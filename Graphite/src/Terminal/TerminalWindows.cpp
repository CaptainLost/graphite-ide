#include "Terminal/TerminalWindows.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/Platform/Process.h"

#include "Settings/AppSettings.h"
#include "UI/RememberedPlace.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
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

	TerminalWindows::TerminalWindows(Workspace& workspace, const Core::Keymap& keys,
								   const Core::CommandRegistry& commands, AppSettings& settings,
								   Core::EventBus& events)
		: m_Workspace(workspace), m_Settings(settings), m_Keys(keys), m_Commands(commands), m_Events(events),
		  m_Shells(Core::DiscoverShells())
	{
	}

	void TerminalWindows::RegisterCommands(Core::CommandRegistry& commands, Core::Keymap& keys)
	{
		commands.Add({.Id = "terminal.new",
					  .Label = "New Terminal",
					  .Icon = ICON_MDI_CONSOLE,
					  .Run = [this] { static_cast<void>(OpenDefaultShell()); },
					  .Enabled = [this] { return !m_Shells.empty(); }});

		// The way back to a terminal once the whole bottom row is closed: with no terminal and
		// no bottom-row panel on screen there is no tab bar left to carry the + button.
		keys.SetDefault("terminal.new",
						{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_GraveAccent)});

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
						  .Run = [this, index] { static_cast<void>(OpenShellIn(index, {})); }}); // logs its own failure
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
		// On the frame the layout was rebuilt, not necessarily the one the reset was asked on. The
		// menu bar is drawn inside DockSpaceHost::Begin, so a reset chosen there comes after this
		// frame's Begin and is built by the next one, whose DockBuilderRemoveNode would undock
		// every terminal placed here again. Waiting for LayoutRebuilt holds wherever the request
		// came from: by the time this runs on that frame, the new nodes exist and the panels are
		// already docked into them, so the terminals join them in the same frame.
		if (m_RedockPending && targets.LayoutRebuilt)
		{
			m_RedockPending = false;

			for (const Core::Scope<Entry>& entry : m_Open)
			{
				entry->Fresh = true;
				entry->DockNode = 0;
				entry->Restored = false;
			}

			// The node the last-focused terminal was in is gone, and "beside it" would only put
			// each terminal next to one placed a moment earlier; the console's row is the place a
			// reset gives them.
			m_LastFocused.clear();
		}

		// Counted by position, so the tabs always read 1..N; the identity after ### is what ImGui
		// keys each window on, so renumbering moves nothing.
		std::size_t number = 0;

		for (const Core::Scope<Entry>& owned : m_Open)
		{
			Entry& entry = *owned;
			++number;

			// The title the shell announced last time stands until it announces one now, so a
			// restored tab reads as it did; what is announced is remembered for the next launch.
			const std::string& live = entry.Session->GetScreen().GetTitle();

			if (!live.empty())
			{
				entry.Record.Title = live;
			}

			const std::string identity = WindowIdentity(entry.Record.Id);
			const std::string name = ICON_MDI_CONSOLE "  " + std::to_string(number) + ": " + TerminalTitleFor(TerminalLabelTitle(live, entry.Record.Title), entry.Session->GetProfileName()) + identity;

			// Its first frame measures a window that is still settling into its dock node.
			const bool appearing = entry.Fresh;

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

			// A terminal floating on its own keeps a node, and so a tab bar, rather than becoming a
			// bare window with a title bar: the bar is where the + and the tab menu are. The class
			// id stays 0, the one every other window has, so a terminal still docks with anything.
			ImGuiWindowClass windowClass;
			windowClass.DockingAlwaysTabBar = true;
			ImGui::SetNextWindowClass(&windowClass);

			bool open = true;
			const bool visible = ImGui::Begin(name.c_str(), &open, flags);

			// A tab's menu asked for this one last frame: the same as its close button.
			if (closeRequests.Take(name))
			{
				open = false;
			}

			// With no shell to offer, nothing extra - not even the separator the menu draws below
			// whatever it is given.
			std::function<void()> newTerminalItems;

			if (!m_Shells.empty())
			{
				newTerminalItems = [this, dockNode = ImGui::GetWindowDockID()] {
					if (ImGui::BeginMenu("New Terminal"))
					{
						DrawShellMenuItems(dockNode);
						ImGui::EndMenu();
					}
				};
			}

			Core::DrawTabContextMenu(closeRequests, newTerminalItems);

			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				m_LastFocused = entry.Record.Id;
			}

			if (visible)
			{
				const Core::CursorPosition fitted = entry.View->Draw(palette);
				const TerminalGridSize measured{fitted.Column, fitted.Row};

				// What fitted is what the shell is told about, which is what makes dragging the
				// window's edge behave like resizing a console window.
				// Not a sliver: the shell would reflow its banner away (TerminalSizeIsPlausible).
				if (!appearing && TerminalSizeIsPlausible(measured, entry.LastMeasured))
				{
					entry.Session->Resize(fitted.Column, fitted.Row);
				}

				// The appearing frame's sliver is no evidence of a size the user chose.
				entry.LastMeasured = appearing ? TerminalGridSize{} : measured;

				// After the draw, so the menu sits over the grid it acts on. Closing is this
				// class's because the session is; the view only reports the choice.
				if (entry.View->DrawContextMenu())
				{
					open = false;
				}
			}

			// Begin() returning false still needs its End().
			ImGui::End();

			entry.Closing = entry.Closing || !open;
		}

		// After every window's End, never inside one: erasing destroys a session, whose destructor
		// terminates the process and joins the reader thread.
		RemoveClosed();

		RenderTabBarButtons(targets);

		// Last, once: the loop above is done with m_Open, which a start appends to, and the tab
		// bars are closed again, so nothing is begun inside a host window re-opened for them. A
		// start only appends an entry - its window is first begun by the next frame's loop - so
		// one pass here, after the buttons rather than also before them, costs no frame.
		StartPendingOpens();
	}

	void TerminalWindows::DrawShellMenuItems(unsigned int dockNode)
	{
		for (std::size_t index = 0; index < m_Shells.size(); ++index)
		{
			if (ImGui::MenuItem(m_Shells[index].Name.c_str()))
			{
				m_PendingOpens.push_back(PendingOpen{index, dockNode});
			}
		}
	}

	void TerminalWindows::RenderTabBarButtons(const TerminalDockTargets& targets)
	{
		if (m_Shells.empty())
		{
			return;
		}

		// Every node a terminal is shown in, and every node a bottom-row panel is: each gets one
		// pair of buttons, however many of them share it.
		std::vector<ImGuiDockNode*> nodes;

		const auto collect = [&nodes](const std::string& windowName) {
			ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());

			if (window != nullptr && window->DockIsActive && window->DockNode != nullptr && std::find(nodes.begin(), nodes.end(), window->DockNode) == nodes.end())
			{
				nodes.push_back(window->DockNode);
			}
		};

		for (const Core::Scope<Entry>& entry : m_Open)
		{
			collect(WindowIdentity(entry->Record.Id));
		}

		for (const std::string& window : targets.RowWindows)
		{
			collect(window);
		}

		for (ImGuiDockNode* node : nodes)
		{
			// Only a bar drawn this frame and still drawable can be appended to: the amend begins
			// the host window again and appends to its tab bar, and asserts if that bar would
			// instead start over - not drawn yet this frame, hidden, or in a host that skips its
			// items, which is what a collapsed floating node is.
			const bool amendable = node->TabBar != nullptr && !node->IsHiddenTabBar() && !node->IsNoTabBar() && node->HostWindow != nullptr && !node->HostWindow->SkipItems && node->TabBar->CurrFrameVisible == ImGui::GetFrameCount();

			if (!amendable)
			{
				continue;
			}

			if (ImGui::DockNodeBeginAmendTabBar(node))
			{
				// Every node's buttons and popup must have ids of their own, and the node's id is in
				// each label rather than pushed. The amend has already pushed node->ID as the seed,
				// and PushID(node->ID) on top of that hashes a value under itself - a CRC32 that
				// comes out the same constant for every node - so every bar's + got one id, and
				// only the first bar drawn could be clicked. Under the amend's seed and with the
				// node's id in the label, the ids differ even if that seed ever stops differing.
				const std::string suffix = std::to_string(node->ID);
				const std::string newLabel = ICON_MDI_PLUS "##terminalNew" + suffix;
				const std::string shellsLabel = ICON_MDI_CHEVRON_DOWN "##terminalShellsButton" + suffix;
				const std::string shellsPopup = "##terminalShells" + suffix;

				if (ImGui::TabItemButton(newLabel.c_str(), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				{
					m_PendingOpens.push_back(PendingOpen{GetDefaultShellIndex(), node->ID});
				}

				ImGui::SetItemTooltip("New terminal");

				if (ImGui::TabItemButton(shellsLabel.c_str(), ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
				{
					ImGui::OpenPopup(shellsPopup.c_str());
				}

				ImGui::SetItemTooltip("New terminal with...");

				if (ImGui::BeginPopup(shellsPopup.c_str()))
				{
					DrawShellMenuItems(node->ID);
					ImGui::EndPopup();
				}

				ImGui::DockNodeEndAmendTabBar();
			}
		}
	}

	void TerminalWindows::StartPendingOpens()
	{
		// Taken first: a start that fails leaves nothing behind to be retried every frame.
		const std::vector<PendingOpen> pending = std::exchange(m_PendingOpens, {});

		for (const PendingOpen& open : pending)
		{
			static_cast<void>(StartShell(open.Shell, {}, open.DockNode)); // logs its own failure
		}
	}

	void TerminalWindows::PlaceNewWindow(const std::string& name, Entry& entry, const TerminalDockTargets& targets)
	{
		// Into the node whose + or tab menu asked for it: the reader pointed at that bar, which
		// says more than which terminal had focus last. Unless the node went away meanwhile.
		if (IsLiveNode(entry.DockNode))
		{
			ImGui::DockBuilderDockWindow(name.c_str(), entry.DockNode);
			return;
		}

		// Beside the terminal used last, wherever the reader put it. A FindEntry guard, because
		// the one used last may have closed since. Floating on its own counts too: with
		// DockingAlwaysTabBar every terminal's first Begin gives it a node, so a window that was
		// drawn last frame always has one to dock into.
		if (!m_LastFocused.empty() && m_LastFocused != entry.Record.Id && FindEntry(m_LastFocused) != nullptr)
		{
			if (ImGuiWindow* last = FindLiveWindow(WindowIdentity(m_LastFocused));
				last != nullptr && IsLiveNode(last->DockId))
			{
				ImGui::DockBuilderDockWindow(name.c_str(), last->DockId);
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

		// Beside any other terminal that has a node, before a new row is made for this one:
		// several terminals restored in one frame with no place remembered, none of them focused
		// yet, would otherwise each split a row of its own. By name rather than FindLiveWindow,
		// because one placed earlier this frame has been begun but was not yet active last frame.
		for (const Core::Scope<Entry>& other : m_Open)
		{
			if (other.get() == &entry || other->Fresh)
			{
				continue;
			}

			if (ImGuiWindow* window = ImGui::FindWindowByName(WindowIdentity(other->Record.Id).c_str());
				window != nullptr && IsLiveNode(window->DockId))
			{
				ImGui::DockBuilderDockWindow(name.c_str(), window->DockId);
				return;
			}
		}

		// The whole bottom row is closed: make it again, as a layout reset would - split off the
		// bottom of the central node, so it sits under the documents and beside the left column
		// rather than under both. Splitting the dockspace's root would also work (ImGui splits an
		// already split node, which is how docking against the dockspace's outer edge works) but
		// would give a row the width of the window, which no layout of this application has. The
		// documents and the central flag go to the upper half, the one the split leaves the old
		// contents in. Only a leaf can be split this way: a CentreNode split earlier this frame
		// is a parent now, and its id is stale until the next frame's targets.
		if (targets.DockSpace != 0)
		{
			if (const ImGuiDockNode* centre = ImGui::DockBuilderGetNode(targets.CentreNode);
				centre != nullptr && centre->IsLeafNode())
			{
				const ImGuiID bottom =
					ImGui::DockBuilderSplitNode(targets.CentreNode, ImGuiDir_Down, targets.BottomRatio, nullptr, nullptr);
				ImGui::DockBuilderDockWindow(name.c_str(), bottom);
				ImGui::DockBuilderFinish(targets.DockSpace);
				return;
			}
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
			m_Events.Publish(ApplicationToast(Core::ToastKind::Warning, "No shell found",
											  "No shell was found on this machine to open a terminal with"));
			return false;
		}

		return OpenDefaultShellIn({});
	}

	bool TerminalWindows::OpenDefaultShellIn(const std::filesystem::path& directory)
	{
		if (m_Shells.empty())
		{
			APP_WARN("No shell found to open a terminal with");
			m_Events.Publish(ApplicationToast(Core::ToastKind::Warning, "No shell found",
											  "No shell was found on this machine to open a terminal with"));
			return false;
		}

		return OpenShellIn(GetDefaultShellIndex(), directory);
	}

	std::size_t TerminalWindows::GetDefaultShellIndex() const
	{
		return DefaultShellIndex(m_Shells, m_Settings.GetDefaultShell());
	}

	bool TerminalWindows::OpenShellIn(std::size_t shell, const std::filesystem::path& directory)
	{
		return StartShell(shell, directory, 0);
	}

	bool TerminalWindows::StartShell(std::size_t shell, const std::filesystem::path& directory, unsigned int dockNode)
	{
		if (shell >= m_Shells.size())
		{
			APP_WARN("No shell {} to open a terminal with: {} found", shell, m_Shells.size());
			m_Events.Publish(ApplicationToast(Core::ToastKind::Warning, "No shell found",
											  std::format("Shell {} is not available: {} were found", shell, m_Shells.size())));
			return false;
		}

		const std::filesystem::path workingDirectory = directory.empty() ? DefaultWorkingDirectory() : directory;

		if (!Start(m_Shells[shell], workingDirectory,
				   OpenTerminal{Core::Uuid::Generate().ToString(), m_Shells[shell].Name, workingDirectory, false, false, {},
								!m_Workspace.IsOpen()},
				   false))
		{
			return false;
		}

		m_Open.back()->DockNode = dockNode;

		// A shell the reader started makes the open folder's list one worth keeping, even once it
		// is closed again. A command's window does not come through here.
		if (m_Workspace.IsOpen())
		{
			m_KeepsTerminals = true;
			m_HeldBack.reset();
		}

		return true;
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
			m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Terminal failed to start",
											  std::format("{}: {}", profile.Name, result.GetError().Message)));
			return false;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Session = std::move(result).GetValue();
		entry->View = Core::CreateScope<TerminalView>(*entry->Session, m_Keys, m_Commands, m_Events);
		entry->Record = std::move(record);
		entry->Restored = restored;

		// A restored window does not pull itself forward; one the reader asked for does.
		entry->Focus = !restored;

		m_Open.push_back(std::move(entry));
		return true;
	}

	std::filesystem::path TerminalWindows::DefaultWorkingDirectory() const
	{
		if (m_Workspace.IsOpen())
		{
			return m_Workspace.GetRoot();
		}

		// No folder: the user's home, not wherever the process happened to be launched from.
		const Core::Result<std::filesystem::path> home = Core::HomeDirectory();

		if (home)
		{
			return home.GetValue();
		}

		APP_TRACE("No home directory for a terminal to start in: {}", home.GetError().Message);

		// The overload that cannot throw: a working directory deleted from under the process
		// makes the other one raise out of a frame (ADR 0004). Empty then, which callers take
		// as "no directory" and let the child process start in its own default.
		std::error_code ignored;
		return std::filesystem::current_path(ignored);
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

	std::optional<std::vector<TerminalTabState>> TerminalWindows::CaptureState() const
	{
		return CaptureFolderTerminals(Records(), m_KeepsTerminals, m_HeldBack);
	}

	void TerminalWindows::RequestRedock()
	{
		m_RedockPending = true;
	}

	void TerminalWindows::CarryOverOpenTerminals()
	{
		std::vector<OpenTerminal> records = Records();
		CarryOverTerminals(records);
		StoreRecords(records);
	}

	void TerminalWindows::StoreRecords(const std::vector<OpenTerminal>& records)
	{
		for (std::size_t index = 0; index < records.size(); ++index)
		{
			m_Open[index]->Record = records[index];
		}
	}

	bool TerminalWindows::OpenStartupShell()
	{
		if (!OpenDefaultShell())
		{
			return false;
		}

		m_Open.back()->Record.AutoOpened = true;
		return true;
	}

	void TerminalWindows::RestoreState(const std::optional<std::vector<TerminalTabState>>& saved, bool trusted)
	{
		// For each folder anew, like m_KeepsTerminals below.
		m_HeldBack = HeldBackTerminals(saved, trusted);

		// The folder just opened takes over the shells nobody had, and from here they are its own:
		// saved with it, and carried over when it closes. A folder in restricted mode takes none.
		std::vector<OpenTerminal> records = Records();
		const bool adopted = AdoptTerminals(records, saved.has_value(), trusted);
		StoreRecords(records);

		if (OpensDefaultTerminal(saved, m_Open.size(), trusted))
		{
			// Kept only if the shell actually started: a folder that got none because no shell
			// was found, or it failed, must still ask for its default at the next launch.
			const bool opened = OpenDefaultShell(); // logs its own failure
			m_KeepsTerminals = KeepsTerminals(saved, opened);
			return;
		}

		// For each folder anew: what the folder before had says nothing about this one, but a
		// shell it adopted is its own.
		m_KeepsTerminals = KeepsTerminals(saved, adopted);

		if (!saved)
		{
			return;
		}

		const std::filesystem::path root = DefaultWorkingDirectory();

		const TerminalRestorePlan plan = PlanTerminalRestore(
			*saved, m_Shells, Records(), [&root](const std::filesystem::path& directory) {
				std::error_code error;
				return !directory.empty() && std::filesystem::is_directory(directory, error) ? directory : root;
			},
			trusted);

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
									OpenTerminal{start.Id, shell.Name, start.WorkingDirectory, false, false, start.Title},
									true));
		}
	}

	void TerminalWindows::RemoveClosed()
	{
		// A shell that exited on its own and a window the reader closed leave by the same door.
		const auto done = [](const Core::Scope<Entry>& entry) { return entry->Closing || !entry->Session->IsAlive(); };

		m_Open.erase(std::remove_if(m_Open.begin(), m_Open.end(), done), m_Open.end());
	}
}
