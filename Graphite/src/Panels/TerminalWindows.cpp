#include "Core/Base/Base.h"

#include "Panels/TerminalWindows.h"

#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/TabContextMenu.h"

#include "UI/RememberedPlace.h"
#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		// What a session starts at. The first frame measures the window and resizes it to what
		// actually fitted, so this only has to be a shape a shell can print its prompt into.
		constexpr std::uint16_t kInitialColumns = 80;
		constexpr std::uint16_t kInitialRows = 25;

		// What ImGui knows a terminal's window by, without the visible half: imgui.ini keeps the
		// window's place under this, so it must never change for a kept terminal.
		std::string WindowIdentity(const std::string& id)
		{
			return "###terminal-" + id;
		}

		ImGuiWindow* FindLiveWindow(const std::string& name)
		{
			ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
			return window != nullptr && window->WasActive ? window : nullptr;
		}

		bool IsLiveNode(ImGuiID node)
		{
			return node != 0 && ImGui::DockBuilderGetNode(node) != nullptr;
		}
	}

	TerminalWindows::TerminalWindows(Workspace& workspace, const Core::Keymap& keys)
		: m_Workspace(workspace), m_Keys(keys), m_Shells(Core::DiscoverShells())
	{
	}

	void TerminalWindows::RegisterCommands(Core::CommandRegistry& commands)
	{
		commands.Add({.Id = "terminal.new",
					  .Label = "New Terminal",
					  .Icon = ICON_MDI_CONSOLE,
					  .Run = [this] { static_cast<void>(OpenDefaultShell()); },
					  .Enabled = [this] { return !m_Shells.empty(); }});

		for (std::size_t index = 0; index < m_Shells.size(); ++index)
		{
			const std::string id = NewTerminalCommandId(m_Shells[index].Name);

			// Two shells whose names slug alike would claim one id; the first, the more capable
			// one in discovery order, keeps it.
			if (commands.Find(id) != nullptr)
			{
				continue;
			}

			commands.Add({.Id = id,
						  .Label = "New " + m_Shells[index].Name,
						  .Group = "terminal.shells",
						  .Run = [this, index] {
							  static_cast<void>(Start(m_Shells[index], DefaultWorkingDirectory(),
													  OpenTerminal{Core::Uuid::Generate().ToString(), m_Shells[index].Name,
																   DefaultWorkingDirectory()},
													  false));
						  }});
		}
	}

	void TerminalWindows::Update()
	{
		// Every session, not only the visible ones: a build running in a tab behind another must
		// keep draining its pipe, or the shell blocks on a full pseudo-console and stops.
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			entry->Session->Pump();
		}
	}

	void TerminalWindows::Render(const TerminalDockTargets& targets, const EditorPalette& palette,
								 Core::TabCloseRequests& closeRequests)
	{
		// Counted by position, so the tabs always read 1..N; the identity after ### is what ImGui
		// keys each window on, so renumbering moves nothing.
		std::size_t number = 0;

		for (const Core::Scope<Entry>& owned : m_Open)
		{
			Entry& entry = *owned;
			++number;

			const std::string identity = WindowIdentity(entry.Record.Id);
			const std::string name = ICON_MDI_CONSOLE "  " + std::to_string(number) + ": "
									 + TerminalTitleFor(entry.Session->GetScreen().GetTitle(),
														entry.Session->GetProfileName())
									 + identity;

			// Docked only on the frame this identity is new, as SessionWindows does: every frame
			// would fight a reader who dragged the tab elsewhere.
			bool restored = false;

			if (std::exchange(entry.Fresh, false))
			{
				restored = std::exchange(entry.Restored, false);

				if (!restored || !HasRememberedPlace(name))
				{
					PlaceNewWindow(name, entry, targets);
				}
			}

			if (std::exchange(entry.Focus, false))
			{
				ImGui::SetNextWindowFocus();
			}

			// Restored windows must not take focus on the frame they appear, or the last one drawn
			// would take the front of its node from whatever imgui.ini remembers there.
			const ImGuiWindowFlags flags = restored ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

			bool open = true;
			const bool visible = ImGui::Begin(name.c_str(), &open, flags);

			// A tab's menu asked for this one last frame: the same as its close button.
			if (closeRequests.Take(name))
			{
				open = false;
			}

			Core::DrawTabContextMenu(closeRequests);

			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				m_LastFocused = entry.Record.Id;
			}

			if (visible)
			{
				const Core::CursorPosition fitted = entry.View->Draw(palette);

				// What fitted is what the shell is told about, which is what makes dragging the
				// window's edge behave like resizing a console window.
				entry.Session->Resize(fitted.Column, fitted.Row);

				// After the draw, so the menu sits over the grid it acts on. Closing is this
				// class's because the session is; the view only reports the choice.
				if (entry.View->DrawContextMenu())
				{
					open = false;
				}
			}

			// Begin() returning false still needs its End().
			ImGui::End();

			// The window exists now, so it can be queued onto the floating terminal it joins.
			if (!entry.DockOnto.empty())
			{
				ImGuiWindow* target = FindLiveWindow(entry.DockOnto);
				ImGuiWindow* payload = ImGui::FindWindowByName(name.c_str());

				if (target != nullptr && payload != nullptr && target != payload)
				{
					ImGui::DockContextQueueDock(ImGui::GetCurrentContext(), target, nullptr, payload,
												ImGuiDir_None, 0.0f, false);
				}

				entry.DockOnto.clear();
			}

			entry.Closing = entry.Closing || !open;
		}

		// After every window's End, never inside one: erasing destroys a session, whose destructor
		// terminates the process and joins the reader thread.
		RemoveClosed();
	}

	void TerminalWindows::PlaceNewWindow(const std::string& name, Entry& entry, const TerminalDockTargets& targets)
	{
		// Beside the terminal used last, wherever the reader put it. A FindEntry guard, because
		// the one used last may have closed since.
		if (!m_LastFocused.empty() && m_LastFocused != entry.Record.Id && FindEntry(m_LastFocused) != nullptr)
		{
			if (ImGuiWindow* last = FindLiveWindow(WindowIdentity(m_LastFocused)))
			{
				if (IsLiveNode(last->DockId))
				{
					ImGui::DockBuilderDockWindow(name.c_str(), last->DockId);
					return;
				}

				// Floating on its own: there is no node to dock into until the new window exists,
				// so it is drawn over the last one for its first frame and then joined to it.
				ImGui::SetNextWindowPos(last->Pos);
				ImGui::SetNextWindowSize(last->Size);
				entry.DockOnto = WindowIdentity(m_LastFocused);
				return;
			}
		}

		// No terminal to stand beside: the bottom row, where the console is.
		if (ImGuiWindow* beside = ImGui::FindWindowByName(targets.BesideWindow.c_str());
			beside != nullptr && IsLiveNode(beside->DockId))
		{
			ImGui::DockBuilderDockWindow(name.c_str(), beside->DockId);
			return;
		}

		if (targets.CentreNode != 0)
		{
			ImGui::DockBuilderDockWindow(name.c_str(), targets.CentreNode);
		}
	}

	bool TerminalWindows::OpenDefaultShell()
	{
		if (m_Shells.empty())
		{
			APP_WARN("No shell found to open a terminal with");
			return false;
		}

		const std::filesystem::path directory = DefaultWorkingDirectory();

		return Start(m_Shells.front(), directory,
					 OpenTerminal{Core::Uuid::Generate().ToString(), m_Shells.front().Name, directory}, false);
	}

	bool TerminalWindows::OpenCommand(const std::string& label, const std::filesystem::path& program,
									  std::vector<std::string> arguments,
									  const std::filesystem::path& workingDirectory)
	{
		// A program is started the way a shell is: a profile is only a name, a path and its
		// arguments, and TerminalSession needs nothing more.
		const Core::ShellProfile profile{label, program, std::move(arguments)};

		return Start(profile, workingDirectory,
					 OpenTerminal{Core::Uuid::Generate().ToString(), label, workingDirectory, true, false}, false);
	}

	bool TerminalWindows::Start(const Core::ShellProfile& profile, const std::filesystem::path& workingDirectory,
								OpenTerminal record, bool restored)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> result =
			Core::TerminalSession::Start(profile, workingDirectory, kInitialColumns, kInitialRows);

		if (!result)
		{
			// No window: an empty one would look like a shell that had simply printed nothing.
			APP_ERROR("Cannot start {}: {}", profile.Name, result.GetError().Message);
			return false;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Session = std::move(result).GetValue();
		entry->View = Core::CreateScope<TerminalView>(*entry->Session, m_Keys);
		entry->Record = std::move(record);
		entry->Restored = restored;

		// A restored window does not pull itself forward; one the reader asked for does.
		entry->Focus = !restored;

		m_Open.push_back(std::move(entry));
		return true;
	}

	std::filesystem::path TerminalWindows::DefaultWorkingDirectory() const
	{
		return m_Workspace.IsOpen() ? m_Workspace.GetRoot() : std::filesystem::current_path();
	}

	TerminalWindows::Entry* TerminalWindows::FindEntry(const std::string& id)
	{
		const auto found = std::find_if(m_Open.begin(), m_Open.end(),
										[&id](const Core::Scope<Entry>& entry) { return entry->Record.Id == id; });
		return found == m_Open.end() ? nullptr : found->get();
	}

	std::vector<OpenTerminal> TerminalWindows::Records() const
	{
		std::vector<OpenTerminal> records;
		records.reserve(m_Open.size());

		for (const Core::Scope<Entry>& entry : m_Open)
		{
			records.push_back(entry->Record);
		}

		return records;
	}

	std::vector<TerminalTabState> TerminalWindows::CaptureState() const
	{
		return CaptureTerminals(Records());
	}

	void TerminalWindows::CarryOverOpenTerminals()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			entry->Record.CarriedOver = true;
		}
	}

	void TerminalWindows::RestoreState(const std::optional<std::vector<TerminalTabState>>& saved)
	{
		if (OpensDefaultTerminal(saved, m_Open.size()))
		{
			static_cast<void>(OpenDefaultShell()); // logs its own failure
			return;
		}

		if (!saved)
		{
			return;
		}

		const std::filesystem::path root = DefaultWorkingDirectory();

		const TerminalRestorePlan plan = PlanTerminalRestore(
			*saved, m_Shells, Records(), [&root](const std::filesystem::path& directory) {
				std::error_code error;
				return !directory.empty() && std::filesystem::is_directory(directory, error) ? directory : root;
			});

		for (const std::string& id : plan.AlreadyOpen)
		{
			if (Entry* entry = FindEntry(id))
			{
				entry->Record.CarriedOver = false;
			}
		}

		for (const std::string& name : plan.UnknownProfiles)
		{
			APP_INFO("Not reopening a {} terminal: no such shell is installed", name);
		}

		for (const TerminalToStart& start : plan.Start)
		{
			const Core::ShellProfile& shell = m_Shells[start.Shell];

			// Start logs its own failure, and the rest still restore.
			static_cast<void>(Start(shell, start.WorkingDirectory,
									OpenTerminal{start.Id, shell.Name, start.WorkingDirectory}, true));
		}
	}

	void TerminalWindows::RemoveClosed()
	{
		// A shell that exited on its own and a window the reader closed leave by the same door.
		const auto done = [](const Core::Scope<Entry>& entry) { return entry->Closing || !entry->Session->IsAlive(); };

		m_Open.erase(std::remove_if(m_Open.begin(), m_Open.end(), done), m_Open.end());
	}
}
