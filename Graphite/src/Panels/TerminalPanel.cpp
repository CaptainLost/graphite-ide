#include "Core/Base/Base.h"

#include "Panels/TerminalPanel.h"

#include "Core/Base/Log.h"

#include "Editor/Palettes.h"
#include "Theme/ActiveScheme.h"
#include "Workspace/Workspace.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kShellMenuId = "##terminalShells";

		// What a session starts at. The first frame measures the panel and resizes it to what
		// actually fitted, so this only has to be a shape a shell can print its prompt into.
		constexpr std::uint16_t kInitialColumns = 80;
		constexpr std::uint16_t kInitialRows = 25;
	}

	TerminalPanel::TerminalPanel(Workspace& workspace, const ActiveScheme& scheme,
								 const Core::Keymap& keys)
		: Core::Panel("terminal", "Terminal"), m_Workspace(workspace), m_Scheme(scheme),
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

		if (m_Tabs.empty())
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float buttonWidth = ImGui::GetFrameHeight() * 2.0f;

			ImGui::SetCursorPos(
				ImVec2(ImGui::GetCursorPos().x + std::max(0.0f, (available.x - buttonWidth) * 0.5f),
					   ImGui::GetCursorPos().y + std::max(0.0f, available.y * 0.5f)));

			if (ImGui::Button("+", ImVec2(buttonWidth, 0.0f)))
			{
				ImGui::OpenPopup(kShellMenuId);
			}

			RenderShellMenu();
			return;
		}

		RenderTabBar();

		// After the bar, never inside it: erasing from the vector the tab items were drawn from
		// would pull the ground out from under ImGui mid-frame.
		RemoveClosedTabs();
	}

	void TerminalPanel::RenderTabBar()
	{
		bool openMenu = false;

		if (ImGui::BeginTabBar("##terminalTabs", ImGuiTabBarFlags_AutoSelectNewTabs
												| ImGuiTabBarFlags_TabListPopupButton))
		{
			if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing
											  | ImGuiTabItemFlags_NoTooltip))
			{
				openMenu = true;
			}

			const EditorPalette palette = ReadEditorPalette(m_Scheme.Get());

			for (Tab& tab : m_Tabs)
			{
				// The shell renames the tab as it works, so the visible half changes while the id
				// after ### stays the session's own for as long as it lives.
				const std::string& title = tab.Session->GetScreen().GetTitle();
				tab.Label = (title.empty() ? tab.Session->GetProfileName() : title)
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

		if (openMenu)
		{
			ImGui::OpenPopup(kShellMenuId);
		}

		RenderShellMenu();
	}

	void TerminalPanel::RenderShellMenu()
	{
		if (!ImGui::BeginPopup(kShellMenuId))
		{
			return;
		}

		if (m_Shells.empty())
		{
			ImGui::TextDisabled("No shell found");
		}

		for (const Core::ShellProfile& profile : m_Shells)
		{
			if (ImGui::MenuItem(profile.Name.c_str()))
			{
				OpenTab(profile);
			}
		}

		ImGui::EndPopup();
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
