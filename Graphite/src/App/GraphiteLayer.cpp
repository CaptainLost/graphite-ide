#include "Core/Base/Base.h"

#include "App/GraphiteLayer.h"

#include "Editor/Palettes.h"
#include "Editor/ServerStatusText.h"
#include "Editor/StatusBar.h"
#include "Editor/TextEditorWidget.h"
#include "Lang/Registry/LanguageRegistry.h"

#include "Git/ChangesPanel.h"
#include "Git/GitPalette.h"
#include "Git/GitView.h"

#include "Panels/ConsolePanel.h"

#include "Panels/ExplorerPanel.h"

#include "Panels/ProblemsPanel.h"
#include "Panels/ReferencesPanel.h"
#include "Panels/SessionsPanel.h"
#include "Panels/TerminalPanel.h"

#include <optional>

#include "Panels/SearchPanel.h"

#include "Settings/AppSettings.h"

#include "App/GraphiteMenu.h"

#include "Core/App/Application.h"

#include "Core/Base/Log.h"

#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Theme.h"

#include "Theme/ChromePalette.h"

#include "Core/Events/ApplicationEvent.h"

#include "Settings/ClaudeSection.h"
#include "Settings/EditorSection.h"
#include "Settings/KeyBindingsSection.h"
#include "Settings/LanguageSection.h"
#include "Settings/SchemeSection.h"
#include "Settings/SearchSection.h"
#include "Settings/SettingsPanel.h"

#include "Core/Fs/FileSystem.h"

#include "Core/Platform/Process.h"

#include <GLFW/glfw3.h>

#include <imgui.h>

#include <cstddef>

#include <utility>

namespace Graphite {

	namespace {
		// The main window alone, not the process: a stale-content sweep only makes sense for
		// the window whose editors are on screen. ImGuiIO::AppFocusLost is not this — the
		// backend sets it on focus *loss*, so it fires as the user leaves rather than as they
		// come back.
		bool MainWindowHasFocus()
		{
			auto* handle =
				static_cast<GLFWwindow*>(Core::Application::Get().GetWindow().GetNativeHandle());

			return glfwGetWindowAttrib(handle, GLFW_FOCUSED) != 0;
		}
	}

	GraphiteLayer::GraphiteLayer(Core::Ref<Core::RingBufferSink> sink,
								 std::filesystem::path initialFolder)
		: Core::Layer("Ide"), m_Sink(std::move(sink)), m_InitialFolder(std::move(initialFolder)),
		  m_CloseQuestion(m_Modals, kUnsavedWorkTitle),
		  m_DropQuestion(m_Modals, kReplaceWorkspaceTitle)
	{
		m_Layout.Left = {"files", "changes"};

		// Beside the console rather than floating: a results list is something you read while
		// looking at the code it points into, which is what the bottom row is for. Without an
		// entry here a panel has nowhere to land and opens as a window of its own.
		m_Layout.Bottom = {"console", "search", "references"};
		m_Layout.Center = {"settings", "git", "sessions"};

		// Only when the caption is ours to draw: the window is allowed to refuse to hide the
		// system one, and then TitleBar::Render draws nothing and the taller row would be
		// empty. The layer is constructed after Window::Create, so the window can answer.
		m_CustomTitleBar = Core::Application::Get().GetWindow().HasCustomTitleBar();

		if (m_CustomTitleBar)
		{
			m_Layout.MenuBarHeight = kCaptionHeight;
		}
	}

	void GraphiteLayer::OnAttach()
	{
		auto* explorer = static_cast<ExplorerPanel*>(
			m_Panels.Add(Core::CreateScope<ExplorerPanel>(m_Workspace, m_Documents)));

		m_Panels.Add(Core::CreateScope<ConsolePanel>(m_Sink));

		m_Panels.Add(Core::CreateScope<TerminalPanel>(m_Workspace, m_ActiveScheme, m_Keys));

		m_SearchPanel = static_cast<SearchPanel*>(m_Panels.Add(Core::CreateScope<SearchPanel>(
			m_Search, m_SearchSettings, m_Workspace, m_Documents, m_ActiveScheme,
			m_EditorSettings, m_PreviewSemantics)));

		m_ReferencesPanel = static_cast<ReferencesPanel*>(m_Panels.Add(
			Core::CreateScope<ReferencesPanel>(m_Workspace, m_Documents, m_ActiveScheme,
											   m_EditorSettings, m_PreviewSemantics)));

		m_Panels.Add(Core::CreateScope<ProblemsPanel>(m_Diagnostics, m_Documents, m_ActiveScheme));

		auto* gitView = static_cast<GitView*>(
			m_Panels.Add(Core::CreateScope<GitView>(m_Git, m_ActiveScheme, m_DiffWindows)));
		gitView->SubscribeTo(m_Events);

		m_Panels.Add(Core::CreateScope<ChangesPanel>(m_Git, m_Modals, m_Documents,
													  m_DocumentWindows, m_ActiveScheme,
													  m_DiffWindows));

		m_ClaudeSettings.Load();

		auto* terminal = static_cast<TerminalPanel*>(m_Panels.Find("terminal"));

		m_Panels.Add(Core::CreateScope<SessionsPanel>(
			m_Workspace, m_ClaudeSettings, m_SessionWindows,
			[this, terminal](const std::string& label, const ClaudeCommand& command,
							 const std::filesystem::path& workingDirectory)
			{
				m_Panels.SetVisible("terminal", true);

				return terminal->OpenCommand(label, command.Program, command.Arguments,
											 workingDirectory);
			}));

		m_Workspace.RegisterCommands(m_Commands);
		m_DocumentWindows.RegisterCommands(m_Commands, m_Keys);
		m_EditorSettings.RegisterCommands(m_Commands, m_Keys);
		explorer->RegisterCommands(m_Commands);
		m_Panels.RegisterCommands(m_Commands);
		RegisterApplicationCommands();

		// After every default registered above, so an override shadows the binding it
		// replaces rather than being shadowed by it.
		AppSettings::LoadKeyBindings(m_Keys);

		// Before any document opens, which is when the first server is started: SetConfiguredBinary
		// deliberately does not touch a server already running, so a setting read after that point
		// would not take effect until the next session.
		if (std::filesystem::path binary = AppSettings::LoadLanguageServerBinary();
			!binary.empty())
		{
			m_LanguageServices.SetConfiguredBinary(LanguageId::Cpp, std::move(binary));
		}

		std::vector<Core::Scope<SettingsSection>> sections;

		sections.push_back(Core::CreateScope<EditorSection>(m_EditorSettings, m_Commands));
		sections.push_back(Core::CreateScope<SchemeSection>(m_ActiveScheme, m_Modals));
		sections.push_back(Core::CreateScope<KeyBindingsSection>(
			m_Commands, m_Keys, [this] { AppSettings::SaveKeyBindings(m_Keys); }));
		sections.push_back(
			Core::CreateScope<SearchSection>(m_SearchSettings, m_SolutionLoaded));
		sections.push_back(Core::CreateScope<LanguageSection>(m_AnalysisSettings));
		sections.push_back(Core::CreateScope<ClaudeSection>(m_ClaudeSettings));

		m_Panels.Add(Core::CreateScope<SettingsPanel>(std::move(sections)));

		ReportConflictingKeyBindings();
		SubscribeToWorkspaceEvents();

		APP_INFO("Registered panels: {}", m_Panels.GetCount());

		explorer->LoadSettings();

		m_EditorSettings.Load();
		m_ActiveScheme.Load();
		m_SearchSettings.Load();

		// Beside the other settings, and before OpenStartupFolder below sets the root the first
		// frame will start a walk for: a pass begun under the default budget and only then
		// resized would evict its way down to the saved one for nothing.
		m_AnalysisSettings.Load();
		m_Analysis.SetBudget(m_AnalysisSettings.GetBudgetBytes());
		m_WorkspaceAnalysis.SetEnabled(m_AnalysisSettings.IsEnabled());

		OpenStartupFolder();

		// Last, after every command there is: the tree names ids, so one built earlier would
		// draw a bar of disabled entries showing raw ids.
		m_MenuTree = BuildMenuTree();
	}

	void GraphiteLayer::RegisterApplicationCommands()
	{
		// "Workspace" rather than "Folder": closing drops the solution and the selection
		// along with the folder.
		m_Commands.Add({.Id = "workspace.close",
						.Label = "Close Workspace",
						.Run = [this] { RequestCloseConfirmation(ClosePurpose::CloseWorkspace); },
						.Enabled = [this] { return m_Workspace.IsOpen(); }});

		// Through the window's close flag, not Application::Close(): the main loop only asks
		// a layer's OnCloseRequested when Window::ShouldClose() is true, and Close() alone
		// never sets it, so this is what puts Exit through the same question as Alt+F4.
		//
		// Alt+F4 is in the label rather than in the keymap: it is Windows' shortcut, and a
		// binding for it would advertise something that cannot be rebound.
		m_Commands.Add({.Id = "app.exit",
						.Label = "Exit\tAlt+F4",
						.Run = [] { Core::Application::Get().GetWindow().RequestClose(); }});

		m_Commands.Add({.Id = "view.resetLayout",
						.Label = "Reset Layout",
						.Run = [this] {
							m_Panels.ResetVisibility();
							m_DockSpace.RequestLayoutReset();
							APP_INFO("Layout restored to the default");
						}});

		// Bound to nothing by default: there is no established chord for it yet, and a command
		// with no binding is still reachable through the command palette and the panels menu.
		m_Commands.Add({.Id = "search.focus",
						.Label = "Find in Files",
						.Run = [this] { m_SearchPanel->Focus(); }});

		// The active document, not the focused one: which editor holds the keyboard is only
		// knowable while drawing, and a command runs before any of them has drawn. Activating a
		// tab is what makes a document the one being worked in, so it is the same document
		// either way in every case a user can produce.
		m_Commands.Add({.Id = "find.open",
						.Label = "Find",
						.Run = [this] {
							if (const auto active = m_Documents.GetActive())
								m_DocumentWindows.OpenFind(m_Documents.At(*active));
						},
						.Enabled = [this] {
							const auto active = m_Documents.GetActive();

							return active && m_Documents.At(*active).Document.has_value();
						}});

		m_Keys.SetDefault("find.open", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_F)});

		// The active document, for the same reason find.open above reads it rather than whatever
		// holds the keyboard: F12 runs before anything has drawn this frame, and activating a tab
		// is what makes it the document a command means either way.
		m_Commands.Add({.Id = "editor.goToDefinition",
						.Label = "Go to Definition",
						.Run = [this] {
							if (const auto active = m_Documents.GetActive())
							{
								m_DocumentWindows.RequestDefinition(*active);
							}
						},
						.Enabled = [this] {
							const auto active = m_Documents.GetActive();

							return active && m_Documents.At(*active).Document.has_value();
						}});

		m_Keys.SetDefault("editor.goToDefinition", {.Primary = Core::Chord::Key(ImGuiKey_F12)});

		// The active document, for the same reason editor.goToDefinition reads it rather than
		// whatever holds the keyboard: Shift+F12 runs before anything has drawn this frame, and
		// activating a tab is what makes it the document a command means either way.
		m_Commands.Add({.Id = "editor.findReferences",
						.Label = "Find All References",
						.Run = [this] {
							if (const auto active = m_Documents.GetActive())
							{
								m_DocumentWindows.RequestReferences(*active, *m_ReferencesPanel);
							}
						},
						.Enabled = [this] {
							const auto active = m_Documents.GetActive();

							return active && m_Documents.At(*active).Document.has_value();
						}});

		m_Keys.SetDefault("editor.findReferences",
						  {.Primary = Core::Chord::Key(ImGuiMod_Shift | ImGuiKey_F12)});

		m_Commands.Add({.Id = "help.logTestEntries",
						.Label = "Log test entries",
						.Run = [] {
							APP_TRACE("Diagnostic entry");
							APP_INFO("Informational entry");
							APP_WARN("Warning entry");
							APP_ERROR("Sample error");
						}});
	}

	void GraphiteLayer::ReportConflictingKeyBindings()
	{
		// The defaults are assembled across five files, each of which believes it is claiming
		// a free chord. An error rather than an assertion: a duplicate binding is a wiring
		// mistake worth shouting about, not a reason to refuse to start.
		for (const Core::Conflict& conflict : m_Keys.FindConflicts())
		{
			APP_ERROR("'{}' and '{}' are both bound to {}", conflict.First, conflict.Second,
					  conflict.Chord.ToString());
		}
	}

	void GraphiteLayer::SubscribeToWorkspaceEvents()
	{
		m_Git.SubscribeTo(m_Events);

		m_Events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			m_GitPort.SetRoot(event.Root);

			// A drop that replaces the workspace publishes no WorkspaceClosedEvent for the
			// folder it replaces - GitModel.cpp documents the same trap and clears on both
			// events for the same reason. Without this, a diff tab opened against the old
			// repository survives into the new one, showing "no longer loaded" and offering
			// a Reload that would ask the new repository for a commit it never had.
			m_DiffWindows.CloseAll();

			// Same trap, same reason: an expectation registered against the folder this drop
			// replaced has no business claiming a path in the one that just opened. Dropping a
			// new folder onto the window is a folder close as far as the user is concerned, even
			// though no WorkspaceClosedEvent says so.
			m_DocumentWindows.ClearExpectedWrites();
		});

		m_Events.Subscribe<OpenFileRequested>([this](const OpenFileRequested& event) {
			m_DocumentWindows.RequestOpen(event.Path, event.Permanent);
		});

		m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			m_Documents.CloseAll();

			// Without this, GitModel stops asking - it handles this same event - but the
			// port's cached repository handles outlive the folder they were opened on,
			// which is exactly what blocks the user's own deletion of .git on Windows.
			m_GitPort.SetRoot({});

			// A diff of a commit in a folder that is no longer open is a tab about nothing.
			m_DiffWindows.CloseAll();

			// An expectation registered against one repository has no business claiming a
			// path in whatever folder opens next. DocumentWindows has no closed-handler of
			// its own to put this in - this event handler is where the rest of the per-folder
			// state above is already dropped, so the expectations join it here.
			m_DocumentWindows.ClearExpectedWrites();
		});

		m_Events.Subscribe<WorkspaceRootMovedEvent>([this](const WorkspaceRootMovedEvent& event) {
			m_DocumentWindows.HandleRootMoved(event.From, event.To);
		});

		m_Events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent& event) {
				m_DocumentWindows.HandleFilesChanged(event.Directories, event.Everything);
			});
	}

	void GraphiteLayer::OpenStartupFolder()
	{
		std::filesystem::path startFolder = m_InitialFolder;

		if (startFolder.empty())
			startFolder = AppSettings::LoadLastFolder();

		if (!startFolder.empty())
			(void)m_Workspace.OpenFolder(startFolder);
	}

	void GraphiteLayer::OnUpdate(Core::Timestep ts)
	{
		m_Workspace.Update();

		// Before the panels, so the model applies whatever a worker finished this frame -
		// and picks up any workspace event just published - before any panel reads a
		// snapshot this frame.
		m_Git.OnUpdate(ts);

		m_Panels.OnUpdate(ts);

		m_SolutionLoaded = m_Workspace.LoadedSolution().has_value();

		UpdateFocusReload(ts);

		m_DocumentWindows.TickExpectedWrites(ts);

		// Watchers drop events — buffer overflow, or a tool touching many files at once — and
		// returning from another application is exactly when that matters.
		m_DocumentWindows.NotifyWindowFocus(MainWindowHasFocus());
	}

	void GraphiteLayer::UpdateFocusReload(Core::Timestep ts)
	{
		// The process, not the main window: a panel dragged out into its own viewport is a
		// separate OS window, and working in one is not having left the application.
		if (!Core::IsForeground())
		{
			m_SecondsInBackground += ts.GetSeconds();
			m_AppForeground = false;
			return;
		}

		if (!m_AppForeground)
		{
			m_AppForeground = true;

			// Reload does nothing without an open folder, but the trace below would still
			// claim a reload happened.
			if (m_SecondsInBackground >= kReloadAfterSecondsAway && m_Workspace.IsOpen())
			{
				APP_TRACE("Reloading the workspace after {:.1f}s away", m_SecondsInBackground);
				m_Workspace.Reload();
			}

			m_SecondsInBackground = 0.0f;
		}
	}

	void GraphiteLayer::ApplyChromeIfSchemeChanged()
	{
		if (m_AppliedSchemeVersion == m_ActiveScheme.GetVersion())
		{
			return;
		}

		m_AppliedSchemeVersion = m_ActiveScheme.GetVersion();

		const Core::ChromePalette palette = ChromePaletteFor(m_ActiveScheme.Get());

		Core::ImGuiRenderer::ApplyTheme(palette);
		Core::Application::Get().GetWindow().SetBorderColor(palette.Border);
	}

	void GraphiteLayer::OnImGuiRender()
	{
		// First statement, before any window is begun: ImGuiStyle::Colors must not change
		// between a Begin and its End.
		ApplyChromeIfSchemeChanged();

		StatusMessages messages{m_DocumentWindows.GetStatusMessage(),
								 m_SearchPanel->GetStatusMessage()};

		// Read once and spent everywhere below - the walk's trigger, the pass, and what the status
		// bar is told about walking. Three separate reads of the same getter are three chances for
		// those to disagree, and a bar saying "finding files" for a walk that was switched off is
		// exactly what disagreement looks like.
		const bool analysisEnabled = m_AnalysisSettings.IsEnabled();

		// The rising edge of that flag, noticed the way the changed root below is: a compare
		// against a remembered value each frame catches every way the setting can move, including
		// from places added later, and costs a bool compare. Switching the feature off cancels the
		// walk and empties the queue, so switching it back on has nothing left to run — without
		// this, it would stay inert until the folder was reopened, which reads as a switch that
		// does not work.
		const bool analysisSwitchedOn = analysisEnabled && !m_AnalysisWasEnabled;

		// The falling edge, for the store alone. The walk is cancelled unconditionally further
		// down, which it can be because Cancel on a walk that is not running does nothing; SetRoot
		// cannot be called that way, because every call tears down the current listing and submits
		// a new one, and a frame that did that sixty times a second would never be ready.
		const bool analysisSwitchedOff = !analysisEnabled && m_AnalysisWasEnabled;

		m_AnalysisWasEnabled = analysisEnabled;

		// Noticed here rather than hooked onto the two places a folder can be opened from: a
		// root comparison per frame catches every way it can change, including ones added later,
		// and costs a path compare against a path that almost never differs.
		if (m_Workspace.GetRoot() != m_WarmedRoot)
		{
			m_WarmedRoot = m_Workspace.GetRoot();

			if (!m_WarmedRoot.empty())
			{
				// Ignored trees skipped: this walk exists to find one source file per language as
				// cheaply as it can, and build outputs hold plenty of files that answer nothing.
				m_LanguageServices.WarmUp(m_WarmedRoot, true);
			}

			// The same trigger as the warm-up above, and the same ignore rule: a tree's build
			// output holds plenty of files a server has nothing to say about.
			//
			// Outside the branch, unlike the warm-up: a folder closing has to drop the queue as
			// surely as a folder opening replaces it.
			m_WorkspaceAnalysis.Clear();

			// The store follows the folder for the same reason the walk does, and on the same
			// comparison: an analysis kept for one workspace says nothing about the next. Outside
			// the branch too — an empty root detaches it, which is what "no folder is open" looks
			// like from there.
			//
			// And the empty root is what a switched-off feature looks like from there as well.
			// Handing the store the real root while the setting is off would create, sweep and
			// fill `.graphite/analysis` under the tree of a user who asked for none of it — the
			// same spending the walk is cancelled below to avoid, and the store's detached path
			// already refuses every lookup and drops every write, so nothing new is needed for it.
			m_AnalysisStore.SetRoot(analysisEnabled ? m_WarmedRoot : std::filesystem::path{});

			if (!m_WarmedRoot.empty() && analysisEnabled)
			{
				m_AnalysisWalk.Start(m_WarmedRoot, true);
			}
			else
			{
				m_AnalysisWalk.Cancel();
			}
		}
		else if (analysisSwitchedOn && !m_WarmedRoot.empty())
		{
			// An `else if` rather than a second branch of its own: a frame that changed the root
			// has already started the walk for it above, and this must not start a second one over
			// the top. Start cancels whatever was running, so a double call would cost a wasted
			// walk rather than corrupt anything - but which branch owns the call is worth being
			// able to read off the code.
			//
			// Nothing to start with no folder open, hence the root check. The queue needs no
			// clearing: switching the feature off emptied it on the way down.
			m_AnalysisWalk.Start(m_WarmedRoot, true);

			// Re-attached here, with the walk, because this is the transition that undoes the
			// detach above: the store was pointed at nothing while the setting was off, and a
			// pass restarted against a detached store would ask the server about every file the
			// last session already answered for.
			m_AnalysisStore.SetRoot(m_WarmedRoot);
		}
		else if (analysisSwitchedOff)
		{
			// The other half of the same rule, and no root check: detaching a store that was
			// already detached costs one empty listing and keeps the two edges symmetrical.
			m_AnalysisStore.SetRoot({});
		}

		// Before anything reads a status, not after. Poll is what turns a warm-up's findings into
		// started servers and what moves a running one from Starting to Indexing to Ready — so a
		// status read ahead of it is always one frame stale, and on the frame a server first
		// appears there is nothing to read at all.
		m_LanguageServices.Poll();

		// Before anything draws, for the reason the Tick below gives: the panels render at 512
		// and DocumentWindows drains its own answers at 516, so a model fed any later is read by
		// the panel one frame before it is written - and on every frame a diagnostic arrives,
		// the squiggle and the panel row disagree.
		DiagnosticsUpdate diagnostics;
		m_LanguageServices.TakeDiagnostics(diagnostics);
		m_Diagnostics.Apply(diagnostics);

		// The same "is this document open" predicate ProblemsPanel filters its own rows by - see
		// DiagnosticsModel::Total's own comment for why.
		const auto isOpenDocument = [this](const std::filesystem::path& path)
		{ return m_Documents.Find(path).has_value(); };

		// Preformatted here rather than handed to StatusBar.cpp as a Counts, the same way Server
		// is preformatted from a ServerStatus below: it keeps DiagnosticsModel out of a file whose
		// only other dependency is Core::DocumentSet. Left unset - not a zeroed string - when
		// both counts are zero, which is what leaves the slot empty rather than reading "0
		// errors".
		if (const DiagnosticsModel::Counts counts = m_Diagnostics.Total(isOpenDocument);
			counts.Errors > 0 || counts.Warnings > 0)
		{
			std::string problems;

			if (counts.Errors > 0)
			{
				problems = std::to_string(counts.Errors) + (counts.Errors == 1 ? " error" : " errors");
			}

			if (counts.Warnings > 0)
			{
				if (!problems.empty())
				{
					problems += ", ";
				}

				problems += std::to_string(counts.Warnings) +
							(counts.Warnings == 1 ? " warning" : " warnings");
			}

			messages.Problems = std::move(problems);
		}

		// Before the panels draw, so what they declare this frame is stamped with this frame's
		// number, and so an entry nothing declared last frame is closed before anything asks
		// about it again.
		m_PreviewSemantics.Tick(static_cast<std::size_t>(ImGui::GetFrameCount()),
								ImGui::GetTime());

		// Read every frame rather than on a change notification: both are a comparison against a
		// value that almost never differs, and a settings object with no observers is one less
		// thing to keep in step. Both setters return without work when nothing moved.
		m_Analysis.SetBudget(m_AnalysisSettings.GetBudgetBytes());
		m_WorkspaceAnalysis.SetEnabled(analysisEnabled);

		// One read, whatever the mode: GetConcurrency answers with the machine's figure while
		// automatic is on and with the chosen number otherwise, so nothing here branches on which.
		m_WorkspaceAnalysis.SetConcurrency(m_AnalysisSettings.GetConcurrency());

		if (!analysisEnabled)
		{
			// Cancelled rather than merely ignored. A walk left running would keep a worker
			// traversing the whole tree to fill a queue Enqueue now drops on the floor - the
			// setting is about not spending the machine on this, and a thread reading every
			// directory under the root is most of that spending.
			m_AnalysisWalk.Cancel();
		}

		// Read before the drain below, not after. The walker can push its last findings and clear
		// this flag between the two calls, and a frame that reported "not walking" while still
		// holding undrained paths would show a Total that jumps the frame after - which is the
		// count appearing to go backwards, the one thing SetWalking exists to prevent. Reading it
		// first costs at worst one extra frame of "finding files", which is the harmless
		// direction.
		const bool walking = analysisEnabled && m_AnalysisWalk.IsWalking();

		// What the walk has found so far, handed over on the UI thread. Drained every frame
		// rather than once at the end: a tree of any size takes a while to walk, and there is no
		// reason for the first file found to wait for the last.
		std::vector<std::filesystem::path> found;
		m_AnalysisWalk.Take(found);

		m_WorkspaceAnalysis.SetWalking(walking);
		m_WorkspaceAnalysis.SetTabWidth(m_EditorSettings.GetIndentStyle().Width);
		m_WorkspaceAnalysis.Enqueue(std::move(found));

		// The open document's own questions come first. IsAnalysing is what the Server slot below
		// already asks to decide whether to say "analysing" rather than "ready", which makes it
		// exactly the condition worth yielding to.
		bool foregroundBusy = false;

		if (const auto analysed = m_Documents.GetActive();
			analysed && m_Documents.At(*analysed).Document)
		{
			foregroundBusy = m_DocumentWindows.IsAnalysing(m_Documents.At(*analysed).Path);
		}

		// Before the pass ticks, for the reason m_LanguageServices.Poll runs before anything reads a
		// status: this is what turns a finished index listing into an answerable store, and a Tick
		// ahead of it spends a frame asking a store that cannot yet say what it already knows.
		//
		// Unconditional, and the Tick below is too: the store states its own readiness to
		// StartNext, which is the only place that has to honour it. Gating the whole Tick here
		// would also stop the pass finishing files whose answers have arrived and timing out ones
		// that have gone silent, neither of which has anything to do with the disk.
		m_AnalysisStore.Poll();

		// "Somebody else already holds this document with the server", not "a tab does".
		// LanguageServices::OpenDocuments counts no references, so whichever of the three openers
		// sends a didClose takes the file away from all of them — and a preview pane holds no
		// DocumentSet entry, so a tab check alone would let the background pass close a document a
		// pane is still asking about. See ADR 0512.
		m_WorkspaceAnalysis.Tick(ImGui::GetTime(), foregroundBusy,
								 [this](const std::filesystem::path& path)
								 {
									 return m_Documents.Find(path).has_value() ||
											m_PreviewSemantics.Holds(path);
								 });

		// EnsureStarted is safe to call every frame the document stays active — see its own
		// comment; the common case is a single map lookup.
		ServerStatus status;
		LanguageId activeLanguage = LanguageId::PlainText;

		if (const auto active = m_Documents.GetActive();
			active && m_Documents.At(*active).Document)
		{
			const std::filesystem::path& path = m_Documents.At(*active).Path;

			activeLanguage = LanguageOfPath(path);

			m_LanguageServices.EnsureStarted(path, activeLanguage);

			status = m_LanguageServices.StatusFor(activeLanguage);
		}

		if (status.State != ServerState::NotConfigured)
		{
			// The active document's own language has something to say, and it outranks everything
			// else: it is the one whose answers are about what is on screen.
			//
			// Named while its answers are still coming, because a ready server and a file whose
			// colours have not arrived look identical from here otherwise - and that gap is
			// exactly when someone wonders whether anything is happening at all.
			const std::filesystem::path& path = m_Documents.At(*m_Documents.GetActive()).Path;

			messages.Server = m_DocumentWindows.IsAnalysing(path)
								  ? AnalysingText(status, Core::ToUtf8(path.filename()))
								  : ServerStatusText(status);

			messages.ServerDetail = ServerStatusDetail(status);
		}
		else if (std::string unserved = NoServerText(activeLanguage); !unserved.empty())
		{
			// A language recognised but unserved - a C# file with no OmniSharp. Said out loud,
			// because the expectation of go to definition is reasonable here and silence would
			// read as something broken rather than something absent.
			messages.Server = std::move(unserved);
			messages.ServerDetail = NoServerDetail(activeLanguage);
		}
		else if (const std::optional<ServerStatus> overall = m_LanguageServices.OverallStatus())
		{
			// Nothing to say about what is open - plain text, or no document at all - so the
			// workspace's servers as a whole. This is what makes a server started by opening the
			// folder visible at all: without it the bar stays blank until a file of its language
			// is opened, which looks like the feature is not working.
			messages.Server = ServerStatusText(*overall);
			messages.ServerDetail = ServerStatusDetail(*overall);
		}

		// Its own slot beside the server's, because it answers a different question: the server
		// phrase is about the file on screen, this one is about the tree behind it. Empty while
		// there is nothing to say, which is what leaves the slot unoccupied rather than resting on
		// a finished count nobody is waiting for.
		if (std::string background = BackgroundAnalysisText(m_WorkspaceAnalysis.Progress());
			!background.empty())
		{
			messages.Background = std::move(background);
			messages.BackgroundDetail = BackgroundAnalysisDetail(
				m_WorkspaceAnalysis.Progress(), m_Analysis.GetBytes(), m_Analysis.GetBudget());
		}

		DrawStatusBar(m_Documents, m_Workspace.GetRoot(), messages);

		m_DockSpace.Begin(m_Layout);
		m_MenuBar.Render(m_Commands, m_Keys, m_MenuTree, m_Layout.MenuBarHeight,
						 [this] { m_TitleBar.Render(Core::Application::Get().GetWindow()); });
		m_DockSpace.End();

		// Outside the dockspace host's Begin/End above, and outside every other window: a
		// modal popup must not be opened from inside a window that may not be current when
		// the popup needs to draw.
		RenderCloseConfirm();
		RenderDropConfirm();

		// Panels render outside the host window — inside it they would dock as ordinary child
		// windows instead of landing in the dockspace nodes.
		m_Panels.OnImGuiRender();

		const CommandContext commandContext{&m_Commands, &m_Keys, &m_Router, &m_FrameInput};

		m_DocumentWindows.Render(commandContext,
								 EditorFrame{.FontSize = m_EditorSettings.GetFontSize(),
											 .Indent = m_EditorSettings.GetIndentStyle(),
											 .FallbackDockNode = m_DockSpace.GetCenterNodeId(),
											 .WordWrap = m_EditorSettings.IsWordWrap(),
											 .Scheme = m_ActiveScheme.Get()},
								 *m_ReferencesPanel);

		m_DiffWindows.Render(m_Git, ReadGitPalette(m_ActiveScheme.Get()),
							 ReadSyntaxPalette(m_ActiveScheme.Get()), m_DockSpace.GetCenterNodeId());

		m_SessionWindows.Render(m_DockSpace.GetCenterNodeId());

		// The last point in the frame that reads the queue: what a frame drains is exactly
		// what arrived since the previous one was drawn.
		m_FrameInput.Clear();
	}

	void GraphiteLayer::OnEvent(Core::Event& event)
	{
		// Before the panels: a drop is not input anybody is competing for, and routing it
		// through the shortcut path would only give the panels a chance to swallow it.
		HandleFileDrop(event);

		if (event.Handled)
		{
			return;
		}

		// The panels first, and only those that drew: a chord being recorded is consumed
		// there, so the router never sees it. Capturing Ctrl+Shift+S would otherwise bind it
		// and save everything in the same keystroke.
		m_Panels.OnEvent(event);

		if (event.Handled)
		{
			return;
		}

		RouteShortcuts(event);

		if (event.Handled)
		{
			return;
		}

		QueueForFocusedEditor(event);
	}

	void GraphiteLayer::HandleFileDrop(Core::Event& event)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::FileDropEvent>([this](Core::FileDropEvent& dropped) {
			const DropRouting routing =
				ClassifyDrop(dropped.GetPaths(), [](const std::filesystem::path& path) {
					std::error_code code;
					return std::filesystem::is_directory(path, code);
				});

			// The workspace before the files, so a file dropped along with the folder it
			// lives in opens into that folder rather than the one being replaced. Waiting
			// for an answer is the exception: the question is not worth holding the files
			// hostage, and opening a folder leaves open documents where they are.
			if (routing.Target != DropTarget::None)
			{
				const DroppedWorkspace workspace{routing.Target, routing.Root};

				if (ReplacesWorkspace(routing, m_Workspace.GetRoot()))
				{
					m_DropQuestion.Ask(workspace);
				}
				else
				{
					OpenDropped(workspace);
				}
			}

			// Permanently, not as a preview: dragging a file onto the window is as deliberate
			// as double-clicking one in the tree.
			for (const std::filesystem::path& file : routing.Files)
			{
				m_Workspace.RequestOpenFile(file, true);
			}

			return true;
		});
	}

	void GraphiteLayer::OpenDropped(const DroppedWorkspace& dropped)
	{
		switch (dropped.Target)
		{
		case DropTarget::None:
			break;

		case DropTarget::Folder:
			// OpenFolder reports its own failures, the way the folder picker's caller leaves
			// them to it.
			(void)m_Workspace.OpenFolder(dropped.Root);
			break;

		case DropTarget::Solution:
			if (const auto opened = m_Workspace.OpenSolution(dropped.Root); !opened)
				APP_ERROR("Cannot open the solution: {}", opened.GetError().Message);
			break;
		}
	}

	void GraphiteLayer::RenderDropConfirm()
	{
		if (!m_DropQuestion.Begin())
		{
			return;
		}

		// Copied out before anything is opened: Answer clears what GetCurrent points at, and
		// opening a workspace is not something to do with a dangling pointer to its path.
		const DroppedWorkspace* asked = m_DropQuestion.GetCurrent();
		const DroppedWorkspace dropped = asked != nullptr ? *asked : DroppedWorkspace{};

		ImGui::TextUnformatted("Open this in place of the workspace now open?");
		ImGui::Separator();
		ImGui::TextUnformatted(Core::ToUtf8(dropped.Root).c_str());
		ImGui::Separator();

		if (ImGui::Button("Open"))
		{
			OpenDropped(dropped);
			m_DropQuestion.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_DropQuestion.Answer();
		}

		m_DropQuestion.End();
	}

	void GraphiteLayer::RouteShortcuts(Core::Event& event)
	{
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			return m_Router.OnKeyPressed(m_Commands, m_Keys, pressed);
		});

		dispatcher.Dispatch<Core::KeyReleasedEvent>([this](Core::KeyReleasedEvent& released) {
			return m_Router.OnKeyReleased(m_Commands, m_Keys, released);
		});

		// A modifier held for a Ctrl+click is not a tap, however it is released afterwards.
		dispatcher.Dispatch<Core::MouseButtonPressedEvent>([this](Core::MouseButtonPressedEvent&) {
			m_Router.CancelTap();
			return false;
		});
	}

	void GraphiteLayer::QueueForFocusedEditor(Core::Event& event)
	{
		// Queued rather than handed to an editor here: which document holds the keyboard is
		// decided by ImGui's window focus, which is only knowable while drawing, and by then
		// the event object is long gone.
		//
		// Both handlers return false: the queue is read by at most one focused editor, and
		// often by none at all, so reporting the key as consumed would claim it landed
		// somewhere.
		Core::EventDispatcher dispatcher{event};

		dispatcher.Dispatch<Core::KeyPressedEvent>([this](Core::KeyPressedEvent& pressed) {
			m_FrameInput.PushKey(pressed.GetKeyCode(), pressed.GetModifiers(), pressed.IsRepeat());
			return false;
		});

		dispatcher.Dispatch<Core::KeyTypedEvent>([this](Core::KeyTypedEvent& typed) {
			m_FrameInput.PushCharacter(typed.GetCodepoint());
			return false;
		});
	}

	bool GraphiteLayer::OnCloseRequested()
	{
		// Save All having succeeded, or Discard All having been chosen, already answered this
		// — see FinishClose. Re-checking for dirty documents here would reopen the modal on
		// Discard All, which never touches document content.
		if (m_CloseApproved)
			return true;

		// Any unresolved close question, not only this purpose's: "Close Workspace" asks the
		// same question, and Alt+F4 reaches here regardless of what ImGui thinks holds input
		// focus, so testing the purpose would overwrite a pending workspace close with an
		// application exit under a dialog the user is already reading.
		if (m_CloseQuestion.IsAsked())
		{
			return false;
		}

		if (!m_DocumentWindows.HasModifiedDocuments())
		{
			return true;
		}

		m_CloseQuestion.Ask(ClosePurpose::ExitApplication);
		return false;
	}

	void GraphiteLayer::RequestCloseConfirmation(ClosePurpose purpose)
	{
		if (!m_DocumentWindows.HasModifiedDocuments())
		{
			FinishClose(purpose);
			return;
		}

		// One close question at a time, whatever raised it. Ask queues rather than replaces,
		// so without this a second "Close Workspace" from the menu would close the workspace
		// twice, once per answer.
		if (m_CloseQuestion.IsAsked())
		{
			return;
		}

		m_CloseQuestion.Ask(purpose);
	}

	void GraphiteLayer::FinishClose(ClosePurpose purpose)
	{
		switch (purpose)
		{
		case ClosePurpose::ExitApplication:
			// OnCloseRequested refusing earlier — to raise this very modal — cleared Window's
			// close flag for good, and nothing but this call re-raises it.
			m_CloseApproved = true;
			Core::Application::Get().GetWindow().RequestClose();
			break;

		case ClosePurpose::CloseWorkspace:
			m_Workspace.Close();
			break;
		}
	}

	bool GraphiteLayer::ConfirmationStillNeeded(ClosePurpose purpose)
	{
		if (m_DocumentWindows.HasModifiedDocuments())
		{
			return true;
		}

		// The question went stale while it waited its turn, the request behind it did not:
		// the user asked to close and nothing now stands in the way.
		FinishClose(purpose);
		return false;
	}

	void GraphiteLayer::ListModifiedDocuments() const
	{
		for (std::size_t index = 0; index < m_Documents.GetCount(); ++index)
		{
			const Core::OpenItem& item = m_Documents.At(index);

			if (item.Document && item.Document->IsModified())
				ImGui::BulletText("%s", Core::ToUtf8(item.Path.filename()).c_str());
		}
	}

	GraphiteLayer::ClosePurpose GraphiteLayer::AskedPurposeOrExit() const
	{
		const ClosePurpose* asked = m_CloseQuestion.GetCurrent();

		return asked != nullptr ? *asked : ClosePurpose::ExitApplication;
	}

	void GraphiteLayer::RenderCloseConfirm()
	{
		if (!m_CloseQuestion.Begin([this](ClosePurpose purpose) {
				return ConfirmationStillNeeded(purpose);
			}))
		{
			return;
		}

		ImGui::TextUnformatted("These files have unsaved changes:");
		ImGui::Separator();

		// Listed afresh every frame, so a Save All that saved some and not others shows what
		// it actually left behind.
		ListModifiedDocuments();

		ImGui::Separator();

		const ClosePurpose purpose = AskedPurposeOrExit();

		if (ImGui::Button("Save All"))
		{
			// A stopped batch and one that skipped a conflicted document both come back
			// false, and both mean some dirty document is not on disk, so the modal stays up
			// rather than closing on a promise it did not keep. SaveAllModified has already
			// put which one happened in the status bar.
			if (m_DocumentWindows.SaveAllModified())
			{
				FinishClose(purpose);
				m_CloseQuestion.Answer();
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Discard All"))
		{
			FinishClose(purpose);
			m_CloseQuestion.Answer();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			m_CloseQuestion.Answer();
		}

		m_CloseQuestion.End();
	}
}
