#include "Sessions/SessionsPanel.h"

#include "Core/Base/Base.h"

#include "Sessions/SessionWindows.h"
#include "Sessions/ClaudeSettings.h"
#include "Sessions/Overview/DashboardView.h"
#include "Sessions/SessionList.h"
#include "Sessions/SessionText.h"
#include "Sessions/SummaryCache.h"
#include "Settings/UserDirectories.h"
#include "Theme/ActiveScheme.h"
#include "Theme/SessionPalette.h"
#include "Workspace/Workspace.h"
#include "UI/Lists/FilterField.h"
#include "UI/Segment.h"
#include "UI/TickBox.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/Theme.h"
#include "Core/Platform/Process.h"
#include "Core/UI/PanelMarginScope.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace Graphite {

	namespace {
		constexpr std::chrono::seconds kDirectoryCheckInterval{5};
		constexpr std::chrono::seconds kCacheSaveInterval{30};
		constexpr std::chrono::steady_clock::time_point kCheckImmediately{};
		constexpr std::chrono::steady_clock::time_point kNeverSaved{};

		constexpr float kFilterWidthInFontSizes = 16.0f;
		const ImVec4 kLaunchProblemColour(0.9f, 0.5f, 0.4f, 1.0f);
	}

	std::filesystem::path SessionsPanel::WorkspaceRootOrCurrentDirectory() const
	{
		if (m_Workspace.IsOpen())
		{
			return m_Workspace.GetRoot();
		}

		// WARNING: Use the overload that cannot throw. A working directory deleted from under the process makes
		// the other one raise out of a frame, and an empty path here means "no directory" to the callers.
		std::error_code status;
		return std::filesystem::current_path(status);
	}

	SessionsPanel::SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings, const ActiveScheme& scheme,
								 SessionWindows& windows, LaunchClaude launch)
		: Core::Panel("sessions", "Claude Sessions", "ai", ICON_LC_BOT, StartsAs::Hidden, Sides::Flush),
		  m_Workspace(workspace), m_Settings(settings), m_Scheme(scheme), m_Windows(windows), m_Launch(std::move(launch))
	{
	}

	SessionsPanelChoice SessionsPanel::GetChoice() const
	{
		return {.Tab = m_Tab, .WorkspaceOnly = m_WorkspaceOnly};
	}

	void SessionsPanel::SetChoice(const SessionsPanelChoice& choice)
	{
		m_Tab = choice.Tab;
		m_WorkspaceOnly = choice.WorkspaceOnly;
	}

	SessionsPanel::~SessionsPanel()
	{
		if (m_CacheDirty)
		{
			SaveCache();
		}
	}

	void SessionsPanel::FindProjectsDirectory()
	{
		const Core::Result<std::filesystem::path> home = Core::HomeDirectory();

		if (!home)
		{
			m_Problem = "Cannot find your home directory: " + home.GetError().Message;
			return;
		}

		m_Projects = ClaudeProjectsDirectory(Core::EnvironmentPath(L"CLAUDE_CONFIG_DIR"), home.GetValue());
	}

	void SessionsPanel::StartIndexingWhenPresent()
	{
		if (m_Indexing || m_Projects.empty())
		{
			return;
		}

		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

		if (now < m_NextDirectoryCheck)
		{
			return;
		}

		m_NextDirectoryCheck = now + kDirectoryCheckInterval;

		// WARNING: Claude Code creates this directory with the first session it writes, so a missing one is not
		// final; it is looked for again.
		std::error_code status;

		if (!std::filesystem::is_directory(m_Projects, status))
		{
			m_Problem = "No Claude transcripts at " + Core::ToUtf8(m_Projects);
			return;
		}

		m_Problem.reset();
		StartIndexing();
	}

	void SessionsPanel::StartIndexing()
	{
		m_Indexing = true;

		m_CacheFile = UserDirectories::SessionsCacheFile();

		std::vector<SessionSummary> cached = m_CacheFile.empty() ? std::vector<SessionSummary>{}
																 : LoadSummaryCache(m_CacheFile);

		for (const SessionSummary& summary : cached)
		{
			m_Sessions[summary.File] = summary;
		}

		m_CacheGaveNothing = cached.empty();

		m_Overview.MarkChanged(SummaryChange::Updated);

		m_Index.Start(m_Projects, std::move(cached));
		m_InitialScan = true;
		m_CacheDirty = true;

		if (Core::Result<Core::Scope<Core::DirectoryWatcher>> watcher = Core::DirectoryWatcher::Start(m_Projects))
		{
			m_Watcher = std::move(watcher).GetValue();
		}
		else
		{
			APP_ERROR("Cannot watch {}: {}", Core::ToUtf8(m_Projects), watcher.GetError().Message);
		}
	}

	void SessionsPanel::TakeFromIndex()
	{
		IndexDelivery delivery;
		m_Index.Take(delivery);

		if (!delivery.Removed.empty())
		{
			m_Overview.MarkChanged(SummaryChange::Removed);
		}
		else if (!delivery.Updated.empty())
		{
			m_Overview.MarkChanged(SummaryChange::Updated);
		}

		for (SessionSummary& summary : delivery.Updated)
		{
			m_Windows.UpdateSummary(summary);
			m_Sessions[summary.File] = std::move(summary);
			m_CacheDirty = true;
		}

		for (const std::filesystem::path& file : delivery.Removed)
		{
			m_Sessions.erase(file);
			m_CacheDirty = true;
		}
	}

	void SessionsPanel::TakeFromWatcher()
	{
		if (!m_Watcher)
		{
			return;
		}

		Core::DirectoryWatcher::Changes changes = m_Watcher->TakeSettled();

		if (changes.Overflowed)
		{
			m_Index.Refresh({m_Projects});
			m_Windows.NotifyChanged({m_Projects});
			return;
		}

		if (changes.Directories.empty())
		{
			return;
		}

		m_Windows.NotifyChanged(changes.Directories);
		m_Index.Refresh(std::move(changes.Directories));
	}

	void SessionsPanel::SaveCacheWhenSettled()
	{
		if (!m_CacheDirty || m_InitialScan)
		{
			return;
		}

		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const bool isFirstSave = m_LastCacheSave == kNeverSaved;

		if (!isFirstSave && now - m_LastCacheSave < kCacheSaveInterval)
		{
			return;
		}

		m_LastCacheSave = now;
		SaveCache();
	}

	void SessionsPanel::SaveCache()
	{
		if (m_CacheFile.empty())
		{
			return;
		}

		std::vector<SessionSummary> summaries;
		summaries.reserve(m_Sessions.size());

		for (const auto& [file, summary] : m_Sessions)
		{
			summaries.push_back(summary);
		}

		if (const Core::Result<void> saved = SaveSummaryCache(m_CacheFile, summaries); !saved)
		{
			APP_ERROR("Cannot write {}: {}", Core::ToUtf8(m_CacheFile), saved.GetError().Message);
		}

		m_CacheDirty = false;
	}

	void SessionsPanel::OnUpdate(Core::Timestep)
	{
		if (!m_Started)
		{
			if (!HasBeenDrawnOnLatestFrame() && !m_Windows.IsLookingForSessions())
			{
				return;
			}

			m_Started = true;
			FindProjectsDirectory();
		}

		StartIndexingWhenPresent();

		if (m_Problem.has_value() && m_Windows.IsLookingForSessions())
		{
			m_Windows.SettleRestoredAsUnfound(*m_Problem);
		}

		if (m_Indexing)
		{
			// WARNING: Progress is read before taking. The worker delivers every summary before it marks the scan
			// done, so a scan reported done here has nothing left to deliver. Read after, the scan could finish
			// in between and leave its last summaries for the next frame.
			const ScanProgress progress = m_Index.InitialScan();
			m_ScanProgress = progress;

			TakeFromIndex();
			TakeFromWatcher();

			if (m_InitialScan && progress.Done)
			{
				m_InitialScan = false;
			}

			if (!m_InitialScan && m_Windows.IsLookingForSessions())
			{
				m_Windows.SettleRestored([this](const std::string& id) { return FindSessionById(id); });
			}

			SaveCacheWhenSettled();
		}

		m_Windows.Update();
	}

	const SessionSummary* SessionsPanel::FindSessionById(const std::string& id) const
	{
		for (const auto& [file, summary] : m_Sessions)
		{
			if (SameSessionId(summary.Id, id))
			{
				return &summary;
			}
		}

		return nullptr;
	}

	void SessionsPanel::StartNewSession()
	{
		const std::filesystem::path program = m_Settings.ResolveProgram();

		if (program.empty())
		{
			m_LaunchProblem = "Claude was not found. Set its path under Settings > Claude.";
			return;
		}

		// WARNING: The id must be lower case, as Claude Code writes it into the transcript's file name; the
		// pending tab is matched to the index's summary by it.
		const std::string id = Core::Lowered(Core::Uuid::Generate().ToString());

		Core::Ref<CommandTerminal> terminal =
			m_Launch("Claude", NewSessionCommand(program, id), WorkspaceRootOrCurrentDirectory());

		if (terminal == nullptr)
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_Windows.OpenPending(id, "New Claude session", std::move(terminal));

		m_NextDirectoryCheck = kCheckImmediately;
	}

	void SessionsPanel::ContinueSession(const SessionSummary& summary)
	{
		const std::filesystem::path program = m_Settings.ResolveProgram();

		if (program.empty())
		{
			m_LaunchProblem = "Claude was not found. Set its path under Settings > Claude.";
			return;
		}

		std::error_code status;
		const bool sessionFolderExists = !summary.Cwd.empty() && std::filesystem::is_directory(summary.Cwd, status);
		const std::filesystem::path directory = sessionFolderExists ? summary.Cwd : WorkspaceRootOrCurrentDirectory();

		if (m_Launch("Claude", ResumeCommand(program, summary.Id), directory) == nullptr)
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_NextDirectoryCheck = kCheckImmediately;
		m_Windows.Open(summary);
	}

	const LocalCalendar& SessionsPanel::Calendar()
	{
		if (!m_Calendar.has_value())
		{
			m_Calendar = LocalCalendar::Current();
		}

		return *m_Calendar;
	}

	std::filesystem::path SessionsPanel::ScopeFolder() const
	{
		return m_WorkspaceOnly && m_Workspace.IsOpen() ? m_Workspace.GetRoot() : std::filesystem::path{};
	}

	void SessionsPanel::RefreshOverview()
	{
		const LocalCalendar& calendar = Calendar();
		m_Overview.Refresh(m_Sessions, m_Settings.GetPricing(), ScopeFolder(), calendar, calendar.Today(),
						   {.Scanning = m_InitialScan, .Now = std::chrono::steady_clock::now()});
	}

	DashboardColours SessionsPanel::Colours() const
	{
		return DashboardColoursFrom(ReadSessionPalette(m_Scheme.Get()), ImGui::GetColorU32(Core::SurfaceColour()));
	}

	void SessionsPanel::RenderToolbar(bool viewIsLoading)
	{
		if (DrawSegment("Overview", m_Tab == SessionsTab::Overview))
		{
			m_Tab = SessionsTab::Overview;
		}

		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

		if (DrawSegment("Sessions", m_Tab == SessionsTab::Sessions))
		{
			m_Tab = SessionsTab::Sessions;
		}

		ImGui::SameLine();

		ImGui::BeginDisabled(!m_Workspace.IsOpen());
		DrawTickBox("This workspace only", &m_WorkspaceOnly);
		ImGui::EndDisabled();

		if (m_Tab == SessionsTab::Sessions)
		{
			ImGui::SameLine();
			DrawFilterField("##sessionFilter", m_Filter,
							{.Hint = "Filter by title", .Width = ImGui::GetFontSize() * kFilterWidthInFontSizes});
		}

		ImGui::SameLine();
		RenderToolbarEnd(viewIsLoading);

		if (m_LaunchProblem.has_value())
		{
			ImGui::TextColored(kLaunchProblemColour, "%s", m_LaunchProblem->c_str());
		}

		// WARNING: Only when the view below shows no loading state of its own, which carries the same line.
		if (m_InitialScan && !viewIsLoading)
		{
			ImGui::TextDisabled("%s", ScanProgressText(m_ScanProgress.Scanned, m_ScanProgress.Total).c_str());
		}
	}

	void SessionsPanel::RenderToolbarEnd(bool viewIsLoading)
	{
		const char* const newSession = ICON_LC_PLUS "  New Claude session";
		const ImGuiStyle& style = ImGui::GetStyle();
		const float start = ImGui::GetCursorPosX();
		const float right = start + ImGui::GetContentRegionAvail().x;
		const float buttonWidth = ImGui::CalcTextSize(newSession).x + style.FramePadding.x * 2.0f;
		const float buttonLeft = std::max(start, right - buttonWidth);

		if (!m_Overview.GetModel().Sessions.empty() && !viewIsLoading)
		{
			const std::string stamp = m_Overview.Stamp();
			const float stampLeft = buttonLeft - style.ItemSpacing.x - ImGui::CalcTextSize(stamp.c_str()).x;

			if (stampLeft >= start)
			{
				ImGui::SetCursorPosX(stampLeft);
				ImGui::TextDisabled("%s", stamp.c_str());
				ImGui::SameLine();
			}
		}

		ImGui::SetCursorPosX(buttonLeft);

		if (Core::TextButton(newSession))
		{
			StartNewSession();
		}
	}

	OverviewContent SessionsPanel::OverviewShows() const
	{
		return OverviewContentFor(m_Indexing, m_InitialScan, m_CacheGaveNothing, !m_Overview.GetModel().Sessions.empty());
	}

	void SessionsPanel::RenderOverview()
	{
		switch (OverviewShows())
		{
		case OverviewContent::Loading:
		{
			const Core::PanelMarginScope margin;
			Core::LoadingState(ScanProgressText(m_ScanProgress.Scanned, m_ScanProgress.Total).c_str());
			return;
		}
		case OverviewContent::Empty:
		{
			const Core::PanelMarginScope margin;
			Core::EmptyState(OverviewEmptyMessage(!ScopeFolder().empty()));
			return;
		}
		case OverviewContent::Dashboard:
			break;
		}

		const DashboardModel& model = m_Overview.GetModel();
		DashboardAction action;

		{
			const Core::PanelMarginScope margin;
			action = DrawDashboard(model, Calendar(), Colours());
		}

		if (action.ShowDay.has_value())
		{
			m_Day = action.ShowDay;
			m_Tab = SessionsTab::Sessions;
		}

		if (action.OpenSession.has_value() && *action.OpenSession < model.Sessions.size())
		{
			m_Windows.Open(*model.Sessions[*action.OpenSession].Summary);
		}
	}

	void SessionsPanel::RenderDayFilter(std::size_t listed)
	{
		if (!m_Day.has_value())
		{
			return;
		}

		const Core::PanelMarginScope margin;
		const std::string chip = DayChipText(*m_Day, listed) + "  " ICON_LC_X;

		if (DrawDayChip(chip, Colours()))
		{
			m_Day.reset();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Show every day");
		}
	}

	std::vector<SessionListRow> SessionsPanel::ListedRows()
	{
		return SessionListRows(m_Overview.GetSessions(), m_Overview.GetModel(),
							   {.Title = m_Filter, .Day = m_Day, .Grouped = ScopeFolder().empty()},
							   [this](const SessionSummary& summary) { return m_Windows.IsOpen(summary.Id); }, Calendar());
	}

	void SessionsPanel::RenderList(const std::vector<SessionListRow>& rows)
	{
		RenderDayFilter(rows.size());

		const bool limitedToWorkspace = !ScopeFolder().empty();

		if (rows.empty())
		{
			const Core::PanelMarginScope margin;
			const bool scanning = SessionScanIsUnderway(m_Indexing, m_InitialScan);
			const std::string message = EmptyListMessage(scanning, m_ScanProgress.Scanned, m_ScanProgress.Total,
														 limitedToWorkspace, m_Filter, m_Day);

			if (scanning)
			{
				Core::LoadingState(message.c_str());
			}
			else
			{
				Core::EmptyState(message);
			}

			return;
		}

		const DashboardColours colours = Colours();
		const SessionListAction action = DrawSessionList(rows, !limitedToWorkspace,
														 {.Prices = m_Settings.GetPricing(),
														  .Model = m_Overview.GetModel(),
														  .Calendar = Calendar(),
														  .Colours = colours,
														  .Clock = m_Clock});

		switch (action.Action)
		{
		case SessionRowAction::Open:
			m_Windows.Open(*action.Summary);
			break;
		case SessionRowAction::Continue:
			ContinueSession(*action.Summary);
			break;
		case SessionRowAction::None:
			break;
		}
	}

	void SessionsPanel::OnImGuiRender()
	{
		// WARNING: First, before anything reads the model or the listed sessions, which point into m_Sessions.
		RefreshOverview();

		const std::vector<SessionListRow> rows = m_Tab == SessionsTab::Sessions ? ListedRows() : std::vector<SessionListRow>{};
		const bool viewIsLoading = m_Tab == SessionsTab::Overview
									   ? OverviewShows() == OverviewContent::Loading
									   : rows.empty() && SessionScanIsUnderway(m_Indexing, m_InitialScan);

		// WARNING: The toolbar is drawn even when there is a problem; "New Claude session" is how a machine
		// with no transcripts gets its first one.
		{
			const Core::PanelMarginScope margin;
			RenderToolbar(viewIsLoading);

			if (m_Problem.has_value())
			{
				ImGui::TextWrapped("%s", m_Problem->c_str());
				return;
			}
		}

		if (m_Tab == SessionsTab::Overview)
		{
			RenderOverview();
		}
		else
		{
			RenderList(rows);
		}
	}
}
