#include "Core/Base/Base.h"

#include "Panels/TerminalPanel.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"

#include "Editor/Palettes.h"
#include "Theme/ActiveScheme.h"
#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		// What a session starts at. The first frame measures the panel and resizes it to what
		// actually fitted, so this only has to be a shape a shell can print its prompt into.
		constexpr std::uint16_t kInitialColumns = 80;
		constexpr std::uint16_t kInitialRows = 25;

		// A shell's announced title is a program's own text, and a shell announces the least
		// useful thing it could: PowerShell and cmd each name their own executable, path and
		// all. That path tells a reader nothing the profile name does not tell them better, so
		// a title that is only the shell introducing itself is dropped. A title a program
		// actually chose - vim naming its file, a build naming its target - is kept, because
		// that is the one worth reading.
		std::string TabLabelFor(const std::string& title, const std::string& profileName)
		{
			if (title.empty())
			{
				return profileName;
			}

			// Only the last component can carry meaning; the directories never do.
			const std::size_t separator = title.find_last_of("\\/");
			const std::string leaf =
				separator == std::string::npos ? title : title.substr(separator + 1);

			if (leaf.empty())
			{
				return profileName;
			}

			return Core::Lowered(leaf).ends_with(".exe") ? profileName : leaf;
		}
	}

	TerminalPanel::TerminalPanel(Workspace& workspace, const ActiveScheme& scheme,
								 const Core::Keymap& keys)
		: Core::Panel("terminal", "Terminal", "tools", ICON_MDI_CONSOLE), m_Workspace(workspace), m_Scheme(scheme),
		  m_Keys(keys), m_Shells(Core::DiscoverShells())
	{
	}

	void TerminalPanel::OnUpdate(Core::Timestep ts)
	{
		(void)ts;

		// Every session, not only the active one: a build running in a tab behind another must
		// keep draining its pipe, or the shell blocks on a full pseudo-console and stops.
		for (Tab& tab : m_Tabs)
		{
			tab.Session->Pump();
		}
	}

	void TerminalPanel::OnImGuiRender()
	{
		if (!m_OpenedFirstTab && !m_Shells.empty())
		{
			m_OpenedFirstTab = true;

			// The discovery list is ordered most capable first, so this is the best default.
			OpenTab(m_Shells.front());
		}

		// Above the tabs, and drawn whether or not any exist: choosing a shell and starting one
		// is how the panel is used when it is empty, and it should not move once it is not.
		RenderNewTerminalRow();

		if (m_Tabs.empty())
		{
			return;
		}

		RenderTabBar();

		// After the bar, never inside it: erasing from the vector the tab items were drawn from
		// would pull the ground out from under ImGui mid-frame.
		RemoveClosedTabs();
	}

	void TerminalPanel::RenderTabBar()
	{
		if (ImGui::BeginTabBar("##terminalTabs", ImGuiTabBarFlags_AutoSelectNewTabs
												| ImGuiTabBarFlags_TabListPopupButton))
		{
			const EditorPalette palette = ReadEditorPalette(m_Scheme.Get());

			// Counted by position rather than by session id, so the strip always reads 1..N.
			// Ids are never reused, so numbering by them would leave gaps as tabs close.
			// Renumbering is safe because what ImGui keys tab state on is the id after ###,
			// not the visible half.
			std::size_t number = 0;

			for (Tab& tab : m_Tabs)
			{
				++number;

				// The shell renames the tab as it works, so the visible half changes while the id
				// after ### stays the session's own for as long as it lives. The number leads
				// because several shells of one profile are otherwise indistinguishable.
				const std::string& title = tab.Session->GetScreen().GetTitle();
				tab.Label = std::to_string(number) + ": "
							+ TabLabelFor(title, tab.Session->GetProfileName())
							+ "###terminalTab" + std::to_string(tab.Id);

				// The × and the middle click both come from p_open, and both only ever set the
				// flag: the tab is removed after the bar, never from inside it.
				bool open = true;

				if (!ImGui::BeginTabItem(tab.Label.c_str(), &open))
				{
					tab.Closing = tab.Closing || !open;
					continue;
				}

				const Core::CursorPosition fitted = tab.View->Draw(palette);

				// What fitted is what the shell is told about, which is what makes dragging the
				// panel edge behave like resizing a console window.
				tab.Session->Resize(fitted.Column, fitted.Row);

				// After the draw, so the menu sits over the grid it acts on. Closing is the
				// panel's because the session is the panel's; the view only reports the choice.
				if (tab.View->DrawContextMenu())
				{
					open = false;
				}

				tab.Closing = tab.Closing || !open;

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}

	void TerminalPanel::RenderNewTerminalRow()
	{
		if (m_Shells.empty())
		{
			ImGui::TextDisabled("No shell found");
			return;
		}

		// Discovery runs once in the constructor, so the list cannot shrink under a stale
		// index; the guard is here because a stale index would silently start the wrong shell.
		if (m_SelectedShell >= m_Shells.size())
		{
			m_SelectedShell = 0;
		}

		// Fixed rather than fitted to the current name, so choosing a shorter shell does not
		// shuffle the + button sideways under the pointer.
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);

		if (ImGui::BeginCombo("##terminalShell", m_Shells[m_SelectedShell].Name.c_str()))
		{
			for (std::size_t index = 0; index < m_Shells.size(); ++index)
			{
				const bool selected = index == m_SelectedShell;

				if (ImGui::Selectable(m_Shells[index].Name.c_str(), selected))
				{
					m_SelectedShell = index;
				}

				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			ImGui::EndCombo();
		}

		ImGui::SameLine();

		if (ImGui::Button("+"))
		{
			OpenTab(m_Shells[m_SelectedShell]);
		}
	}

	void TerminalPanel::OpenTab(const Core::ShellProfile& profile)
	{
		const std::filesystem::path workingDirectory =
			m_Workspace.IsOpen() ? m_Workspace.GetRoot() : std::filesystem::current_path();

		Core::Result<Core::Scope<Core::TerminalSession>> result =
			Core::TerminalSession::Start(profile, workingDirectory, kInitialColumns, kInitialRows);

		if (!result)
		{
			// No tab: an empty one would look like a shell that had simply printed nothing.
			APP_ERROR("Cannot start {}: {}", profile.Name, result.GetError().Message);
			return;
		}

		Tab tab;
		tab.Session = std::move(result).GetValue();
		tab.View = Core::CreateScope<TerminalView>(*tab.Session, m_Keys);
		tab.Id = m_NextId++;

		m_Tabs.push_back(std::move(tab));
	}

	void TerminalPanel::RemoveClosedTabs()
	{
		// A shell that exited on its own and a tab the reader closed leave by the same door.
		// Erasing the Tab destroys its session, whose destructor terminates the process and
		// joins the reader thread — which is why this may not happen inside the tab bar.
		const auto done = [](const Tab& tab) { return tab.Closing || !tab.Session->IsAlive(); };

		if (std::none_of(m_Tabs.begin(), m_Tabs.end(), done))
		{
			return;
		}

		m_Tabs.erase(std::remove_if(m_Tabs.begin(), m_Tabs.end(), done), m_Tabs.end());
	}
}
