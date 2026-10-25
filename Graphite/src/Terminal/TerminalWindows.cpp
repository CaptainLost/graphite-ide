#include "Terminal/TerminalWindows.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/DockTab.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/Platform/Process.h"

#include "Settings/AppSettings.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include <IconsLucide.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::uint16_t kInitialColumns = 80;
		constexpr std::uint16_t kInitialRows = 25;
		constexpr unsigned int kNoDockNode = 0;

		// WARNING: imgui.ini keeps a window's place under this identity, so it must never change for a kept terminal.
		std::string WindowIdentity(const std::string& id)
		{
			return "###terminal-" + id;
		}

		constexpr int kTabBarButtonColourCount = 3;
		constexpr ImGuiTabItemFlags kTabBarButtonFlags = ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip;

		// WARNING: A button in a tab bar is a tab. It is as tall as the bar only with the frame padding the bar is laid
		// out with, and ImGui lays it out in DockSpace with no padding across, so its width is asked for and its glyph
		// drawn centred here. It would fill in the bar's tab colour and cover the line along the bar's bottom, so it is
		// clear at rest and takes the hover surface under the pointer.
		bool TabBarButton(const std::string& id, const char* glyph)
		{
			const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
			const float width = ImGui::CalcTextSize(glyph).x + ImGui::GetStyle().FramePadding.x * 2.0f;

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
								ImVec2(ImGui::GetStyle().FramePadding.x, Core::DockTabBarFramePaddingY()));
			ImGui::PushStyleColor(ImGuiCol_Tab, clear);
			ImGui::PushStyleColor(ImGuiCol_TabDimmed, clear);
			ImGui::PushStyleColor(ImGuiCol_TabHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
			ImGui::SetNextItemWidth(width);
			const bool pressed = ImGui::TabItemButton(id.c_str(), kTabBarButtonFlags);
			ImGui::PopStyleColor(kTabBarButtonColourCount);
			ImGui::PopStyleVar();

			// A button that appears this frame is laid out but not placed, and has no rect yet.
			const ImRect button(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			if (button.GetWidth() > 0.0f)
			{
				Core::DrawCentredGlyph(ImGui::GetWindowDrawList(), button, glyph, ImGui::GetColorU32(ImGuiCol_Text));
			}

			return pressed;
		}

		ImGuiWindow* FindLiveWindow(const std::string& name)
		{
			ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
			return window != nullptr && window->WasActive ? window : nullptr;
		}

		bool IsLiveNode(ImGuiID node)
		{
			return node != kNoDockNode && ImGui::DockBuilderGetNode(node) != nullptr;
		}

		bool ContainsNode(const std::vector<ImGuiDockNode*>& nodes, const ImGuiDockNode* wanted)
		{
			for (const ImGuiDockNode* node : nodes)
			{
				if (node == wanted)
				{
					return true;
				}
			}

			return false;
		}

		void CollectDockNode(const std::string& windowName, std::vector<ImGuiDockNode*>& nodes)
		{
			ImGuiWindow* window = ImGui::FindWindowByName(windowName.c_str());

			if (window != nullptr && window->DockIsActive && window->DockNode != nullptr && !ContainsNode(nodes, window->DockNode))
			{
				nodes.push_back(window->DockNode);
			}
		}

		// WARNING: The amend asserts if the tab bar would start over: not drawn yet this frame, hidden, or in a host that skips its items, which a collapsed floating node is.
		bool IsAmendable(const ImGuiDockNode* node)
		{
			const bool hasDrawableTabBar = node->TabBar != nullptr && !node->IsHiddenTabBar() && !node->IsNoTabBar();
			const bool hasDrawableHost = node->HostWindow != nullptr && !node->HostWindow->SkipItems;

			return hasDrawableTabBar && hasDrawableHost && node->TabBar->CurrFrameVisible == ImGui::GetFrameCount();
		}

		std::filesystem::path ResolveRestoredDirectory(const std::filesystem::path& directory,
													   const std::filesystem::path& fallbackRoot)
		{
			std::error_code error;
			return !directory.empty() && std::filesystem::is_directory(directory, error) ? directory : fallbackRoot;
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
					  .Icon = ICON_LC_SQUARE_TERMINAL,
					  .Run = [this] {
						  Reveal();
						  static_cast<void>(OpenDefaultShell()); },
					  .Enabled = [this] { return !m_Shells.empty(); }});

		keys.SetDefault("terminal.new",
						{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_GraveAccent)});

		for (std::size_t shellIndex = 0; shellIndex < m_Shells.size(); ++shellIndex)
		{
			const std::string commandId = NewTerminalCommandId(m_Shells[shellIndex].Name);

			if (commands.Find(commandId) != nullptr)
			{
				continue;
			}

			commands.Add({.Id = commandId,
						  .Label = "New " + m_Shells[shellIndex].Name,
						  .Group = "terminal.shells",
						  .Run = [this, shellIndex] {
							  Reveal();
							  static_cast<void>(OpenShellIn(shellIndex, std::filesystem::path{})); // logs its own failure
						  }});
		}
	}

	// WARNING: Every session is drained, not only the visible ones: a build in a tab behind another must keep emptying its pipe, or the shell blocks on a full pseudo-console.
	void TerminalWindows::Update()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			entry->Session->ApplyQueuedOutput();
			NoteCommandExit(*entry);
			TakeShowRequest(*entry);
		}
	}

	void TerminalWindows::NoteCommandExit(Entry& entry)
	{
		const bool exitAlreadyNoted = entry.Command == nullptr || entry.Command->Exited;

		if (exitAlreadyNoted || entry.Session->IsAlive())
		{
			return;
		}

		if (!entry.Session->HasFinished())
		{
			Core::Redraw::Request();
			return;
		}

		const std::optional<std::uint32_t> exitCode = entry.Session->GetExitCode();
		WriteProcessExitedLine(entry.Session->GetScreen(), exitCode);

		entry.Command->Exited = true;
		entry.Command->ExitCode = exitCode;
	}

	// WARNING: A program that has ended never repaints, so its screen must not be cut to a smaller window the way a
	// live one is: what the cut dropped would be gone for good.
	void TerminalWindows::ResizeTerminal(Entry& entry, Core::CursorPosition fitted)
	{
		if (entry.Session->IsAlive())
		{
			entry.Session->Resize(fitted.Column, fitted.Row);
			return;
		}

		ResizeEndedScreen(entry.Session->GetScreen(), fitted.Column, fitted.Row);
	}

	void TerminalWindows::TakeShowRequest(Entry& entry)
	{
		if (entry.Command == nullptr || !std::exchange(entry.Command->ShowRequested, false))
		{
			return;
		}

		Reveal();
		entry.Window.Focus = true;
	}

	void TerminalWindows::Render(const TerminalDockTargets& targets, const EditorPalette& palette,
								 Core::TabCloseRequests& closeRequests)
	{
		// WARNING: Waits for LayoutRebuilt, not for the request: a reset chosen in the menu bar arrives after this frame's Begin, and building it on the next frame would undock every terminal placed here again.
		if (m_RedockPending && targets.LayoutRebuilt)
		{
			m_RedockPending = false;

			for (const Core::Scope<Entry>& entry : m_Open)
			{
				entry->Window.Fresh = true;
				entry->DockNode = kNoDockNode;
				entry->Window.Restored = false;
			}

			m_LastFocused.clear();
		}

		RecordAnnouncedTitles();

		if (m_Hidden)
		{
			m_AnyDrawn = false;
		}
		else
		{
			m_AnyDrawn = RenderWindows(targets, palette, closeRequests);
		}

		RemoveClosed();

		RenderTabBarButtons(targets);

		// WARNING: Must stay last: a start appends to m_Open, which RenderWindows iterates, and the tab bars must be closed again.
		StartPendingOpens();
	}

	void TerminalWindows::RecordAnnouncedTitles()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			const std::string& announcedTitle = entry->Session->GetScreen().GetTitle();

			if (!announcedTitle.empty())
			{
				entry->Record.Title = announcedTitle;
			}
		}
	}

	bool TerminalWindows::RenderWindows(const TerminalDockTargets& targets, const EditorPalette& palette,
										Core::TabCloseRequests& closeRequests)
	{
		bool anyDrawn = false;
		std::size_t tabNumber = 0;

		for (const Core::Scope<Entry>& ownedEntry : m_Open)
		{
			Entry& entry = *ownedEntry;
			++tabNumber;

			const std::string& announcedTitle = entry.Session->GetScreen().GetTitle();
			const std::string windowIdentity = WindowIdentity(entry.Record.Id);
			const std::string liveTitle =
				TerminalTitleFor(TerminalLabelTitle(announcedTitle, entry.Record.Title), entry.Session->GetProfileName());
			const bool commandExited = entry.Command != nullptr && entry.Command->Exited;
			const std::string tabTitle = commandExited ? ExitedTerminalTitle(liveTitle) : liveTitle;
			const std::string windowName = ICON_LC_SQUARE_TERMINAL "  " + std::to_string(tabNumber) + ": " + tabTitle + windowIdentity;

			ImGuiWindowClass windowClass;
			windowClass.DockingAlwaysTabBar = true;
			ImGui::SetNextWindowClass(&windowClass);

			const TabWindowFrame frame = BeginTabWindow(
				windowName, entry.Window,
				[this, &entry, &targets](const std::string& name) { PlaceNewWindow(name, entry, targets); }, closeRequests);
			const bool appearing = frame.Appearing;
			bool stayOpen = frame.Open;

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

			if (frame.Visible)
			{
				anyDrawn = true;

				const Core::CursorPosition fitted = entry.View->Draw(palette);
				const TerminalGridSize measured{fitted.Column, fitted.Row};

				if (!appearing && TerminalSizeIsPlausible(measured, entry.LastMeasured))
				{
					ResizeTerminal(entry, fitted);
				}

				entry.LastMeasured = appearing ? TerminalGridSize{} : measured;

				if (entry.View->DrawContextMenu())
				{
					stayOpen = false;
				}
			}

			ImGui::End();

			entry.Closing = entry.Closing || !stayOpen;
		}

		return anyDrawn;
	}

	void TerminalWindows::DrawShellMenuItems(unsigned int dockNode)
	{
		for (std::size_t shellIndex = 0; shellIndex < m_Shells.size(); ++shellIndex)
		{
			if (ImGui::MenuItem(m_Shells[shellIndex].Name.c_str()))
			{
				m_PendingOpens.push_back(PendingOpen{.Shell = shellIndex, .DockNode = dockNode});
			}
		}
	}

	void TerminalWindows::RenderTabBarButtons(const TerminalDockTargets& targets)
	{
		if (m_Shells.empty())
		{
			return;
		}

		std::vector<ImGuiDockNode*> nodes;

		for (const Core::Scope<Entry>& entry : m_Open)
		{
			CollectDockNode(WindowIdentity(entry->Record.Id), nodes);
		}

		for (const std::string& rowWindow : targets.RowWindows)
		{
			CollectDockNode(rowWindow, nodes);
		}

		for (ImGuiDockNode* node : nodes)
		{
			if (!IsAmendable(node))
			{
				continue;
			}

			if (ImGui::DockNodeBeginAmendTabBar(node))
			{
				// WARNING: The node's id goes in each label rather than being pushed: the amend has already pushed it as the seed, and pushing it again hashes a value under itself into the same constant for every node, so only the first bar's + could be clicked.
				const std::string nodeSuffix = std::to_string(node->ID);
				const std::string newLabel = "##terminalNew" + nodeSuffix;
				const std::string shellsLabel = "##terminalShellsButton" + nodeSuffix;
				const std::string shellsPopup = "##terminalShells" + nodeSuffix;

				if (TabBarButton(newLabel, ICON_LC_PLUS))
				{
					m_PendingOpens.push_back(PendingOpen{.Shell = GetDefaultShellIndex(), .DockNode = node->ID});
				}

				ImGui::SetItemTooltip("New terminal");

				if (TabBarButton(shellsLabel, ICON_LC_CHEVRON_DOWN))
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
		if (m_PendingOpens.empty())
		{
			return;
		}

		Reveal();

		const std::vector<PendingOpen> pendingOpens = std::move(m_PendingOpens);
		m_PendingOpens.clear();

		for (const PendingOpen& pendingOpen : pendingOpens)
		{
			static_cast<void>(StartShell(pendingOpen.Shell, std::filesystem::path{}, pendingOpen.DockNode)); // logs its own failure
		}
	}

	void TerminalWindows::PlaceNewWindow(const std::string& name, Entry& entry, const TerminalDockTargets& targets)
	{
		if (IsLiveNode(entry.DockNode))
		{
			ImGui::DockBuilderDockWindow(name.c_str(), entry.DockNode);
			return;
		}

		if (!m_LastFocused.empty() && m_LastFocused != entry.Record.Id && FindEntry(m_LastFocused) != nullptr)
		{
			if (ImGuiWindow* lastFocusedWindow = FindLiveWindow(WindowIdentity(m_LastFocused));
				lastFocusedWindow != nullptr && IsLiveNode(lastFocusedWindow->DockId))
			{
				ImGui::DockBuilderDockWindow(name.c_str(), lastFocusedWindow->DockId);
				return;
			}
		}

		if (ImGuiWindow* besideWindow = ImGui::FindWindowByName(targets.BesideWindow.c_str());
			besideWindow != nullptr && IsLiveNode(besideWindow->DockId))
		{
			ImGui::DockBuilderDockWindow(name.c_str(), besideWindow->DockId);
			return;
		}

		// WARNING: Looked up by name, not FindLiveWindow: a terminal placed earlier this frame has been begun but was not yet active last frame.
		for (const Core::Scope<Entry>& other : m_Open)
		{
			if (other.get() == &entry || other->Window.Fresh)
			{
				continue;
			}

			if (ImGuiWindow* otherWindow = ImGui::FindWindowByName(WindowIdentity(other->Record.Id).c_str());
				otherWindow != nullptr && IsLiveNode(otherWindow->DockId))
			{
				ImGui::DockBuilderDockWindow(name.c_str(), otherWindow->DockId);
				return;
			}
		}

		if (targets.DockSpace != 0)
		{
			// WARNING: Only a leaf can be split: a CentreNode split earlier this frame is a parent now, and its id is stale until the next frame's targets.
			if (const ImGuiDockNode* centreNode = ImGui::DockBuilderGetNode(targets.CentreNode);
				centreNode != nullptr && centreNode->IsLeafNode())
			{
				const ImGuiID bottomNode =
					ImGui::DockBuilderSplitNode(targets.CentreNode, ImGuiDir_Down, targets.BottomRatio, nullptr, nullptr);
				ImGui::DockBuilderDockWindow(name.c_str(), bottomNode);
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
		return OpenDefaultShellIn(std::filesystem::path{});
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

	bool TerminalWindows::OpenShellIn(std::size_t shellIndex, const std::filesystem::path& directory)
	{
		return StartShell(shellIndex, directory, kNoDockNode);
	}

	bool TerminalWindows::StartShell(std::size_t shellIndex, const std::filesystem::path& directory, unsigned int dockNode)
	{
		if (shellIndex >= m_Shells.size())
		{
			APP_WARN("No shell {} to open a terminal with: {} found", shellIndex, m_Shells.size());
			m_Events.Publish(ApplicationToast(Core::ToastKind::Warning, "No shell found",
											  std::format("Shell {} is not available: {} were found", shellIndex, m_Shells.size())));
			return false;
		}

		const Core::ShellProfile& profile = m_Shells[shellIndex];
		const std::filesystem::path workingDirectory = directory.empty() ? DefaultWorkingDirectory() : directory;

		OpenTerminal record{.Id = Core::Uuid::Generate().ToString(),
							.ProfileName = profile.Name,
							.WorkingDirectory = workingDirectory,
							.Unowned = !m_Workspace.IsOpen()};

		if (!Start(profile, workingDirectory, std::move(record), EntryOrigin::Requested))
		{
			return false;
		}

		m_Open.back()->DockNode = dockNode;

		if (m_Workspace.IsOpen())
		{
			m_KeepsTerminals = true;
			m_HeldBack.reset();
		}

		return true;
	}

	Core::Ref<CommandTerminal> TerminalWindows::OpenCommand(const std::string& label,
															const std::filesystem::path& program,
															std::vector<std::string> arguments,
															const std::filesystem::path& workingDirectory)
	{
		Reveal();

		const Core::ShellProfile profile{label, program, std::move(arguments)};

		OpenTerminal record{.Id = Core::Uuid::Generate().ToString(),
							.ProfileName = label,
							.WorkingDirectory = workingDirectory,
							.FromCommand = true};

		if (!Start(profile, workingDirectory, std::move(record), EntryOrigin::Requested))
		{
			return nullptr;
		}

		Core::Ref<CommandTerminal> command = Core::CreateRef<CommandTerminal>();
		m_Open.back()->Command = command;

		return command;
	}

	bool TerminalWindows::Start(const Core::ShellProfile& profile, const std::filesystem::path& workingDirectory,
								OpenTerminal record, EntryOrigin origin)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> startResult =
			Core::TerminalSession::Start(profile, workingDirectory, kInitialColumns, kInitialRows);

		if (!startResult)
		{
			APP_ERROR("Cannot start {}: {}", profile.Name, startResult.GetError().Message);
			m_Events.Publish(ApplicationToast(Core::ToastKind::Error, "Terminal failed to start",
											  std::format("{}: {}", profile.Name, startResult.GetError().Message)));
			return false;
		}

		const bool restored = origin == EntryOrigin::Restored;

		Core::Scope<Entry> entry = Core::CreateScope<Entry>();
		entry->Session = std::move(startResult).GetValue();
		entry->View = Core::CreateScope<TerminalView>(*entry->Session, m_Keys, m_Commands, m_Events);
		entry->Record = std::move(record);
		entry->Window.Restored = restored;
		entry->Window.Focus = !restored;

		m_Open.push_back(std::move(entry));
		return true;
	}

	// WARNING: current_path is called with an error code because the throwing overload raises out of a frame when the working directory was deleted from under the process. The empty result then means no directory.
	std::filesystem::path TerminalWindows::DefaultWorkingDirectory() const
	{
		if (m_Workspace.IsOpen())
		{
			return m_Workspace.GetRoot();
		}

		const Core::Result<std::filesystem::path> home = Core::HomeDirectory();

		if (home)
		{
			return home.GetValue();
		}

		APP_TRACE("No home directory for a terminal to start in: {}", home.GetError().Message);

		std::error_code ignored;
		return std::filesystem::current_path(ignored);
	}

	TerminalWindows::Entry* TerminalWindows::FindEntry(const std::string& id)
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (entry->Record.Id == id)
			{
				return entry.get();
			}
		}

		return nullptr;
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
		Reveal();
		m_RedockPending = true;
	}

	void TerminalWindows::SetHidden(bool hidden)
	{
		m_Hidden = hidden;
	}

	void TerminalWindows::Reveal()
	{
		SetHidden(false);
	}

	TerminalStripLook TerminalWindows::GetStripLook() const
	{
		return TerminalStripLookOf(m_Open.size(), m_Hidden);
	}

	void TerminalWindows::ToggleFromStrip()
	{
		switch (ChooseTerminalStripAction(m_Open.size(), m_Hidden, m_AnyDrawn))
		{
		case TerminalStripAction::StartDefault:
			Reveal();
			static_cast<void>(OpenDefaultShell());
			break;
		case TerminalStripAction::Hide:
			SetHidden(true);
			break;
		case TerminalStripAction::Show:
			Reveal();
			BringForwardLastFocused();
			break;
		case TerminalStripAction::BringForward:
			BringForwardLastFocused();
			break;
		}
	}

	void TerminalWindows::BringForwardLastFocused()
	{
		std::vector<std::string> ids;
		ids.reserve(m_Open.size());

		for (const Core::Scope<Entry>& entry : m_Open)
		{
			ids.push_back(entry->Record.Id);
		}

		if (const std::optional<std::size_t> index = TerminalToBringForward(ids, m_LastFocused))
		{
			m_Open[*index]->Window.Focus = true;
		}
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
		m_HeldBack = HeldBackTerminals(saved, trusted);

		std::vector<OpenTerminal> records = Records();
		const bool adopted = AdoptTerminals(records, saved.has_value(), trusted);
		StoreRecords(records);

		if (OpensDefaultTerminal(saved, m_Open.size(), trusted))
		{
			const bool opened = OpenDefaultShell(); // logs its own failure
			m_KeepsTerminals = KeepsTerminals(saved, opened);
			return;
		}

		m_KeepsTerminals = KeepsTerminals(saved, adopted);

		if (!saved)
		{
			return;
		}

		const std::filesystem::path root = DefaultWorkingDirectory();

		const TerminalRestorePlan plan = PlanTerminalRestore(
			*saved, m_Shells, Records(),
			[&root](const std::filesystem::path& directory) { return ResolveRestoredDirectory(directory, root); },
			trusted);

		for (const std::string& alreadyOpenId : plan.AlreadyOpen)
		{
			if (Entry* entry = FindEntry(alreadyOpenId))
			{
				entry->Record.CarriedOver = false;
			}
		}

		for (const std::string& unknownProfile : plan.UnknownProfiles)
		{
			APP_INFO("Not reopening a {} terminal: no such shell is installed", unknownProfile);
		}

		for (const TerminalToStart& terminalToStart : plan.Start)
		{
			const Core::ShellProfile& profile = m_Shells[terminalToStart.Shell];

			OpenTerminal record{.Id = terminalToStart.Id,
								.ProfileName = profile.Name,
								.WorkingDirectory = terminalToStart.WorkingDirectory,
								.Title = terminalToStart.Title};

			static_cast<void>(Start(profile, terminalToStart.WorkingDirectory, std::move(record), EntryOrigin::Restored)); // logs its own failure
		}
	}

	bool TerminalWindows::TabCloses(const Core::Scope<Entry>& entry)
	{
		const TerminalKind kind = entry->Command != nullptr ? TerminalKind::Command : TerminalKind::Shell;
		return TerminalTabCloses(kind, entry->Closing, entry->Session->IsAlive());
	}

	// WARNING: Call only after every window's End: erasing destroys a session, whose destructor terminates the process and joins the reader thread.
	void TerminalWindows::RemoveClosed()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (entry->Command != nullptr && TabCloses(entry))
			{
				entry->Command->Closed = true;
			}
		}

		std::erase_if(m_Open, TabCloses);
	}
}
