#include "Sessions/SessionsPanel.h"

#include "Core/Base/Base.h"

#include "Sessions/SessionWindows.h"
#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionText.h"
#include "Sessions/SummaryCache.h"
#include "Settings/UserDirectories.h"
#include "Workspace/Workspace.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "UI/FittedText.h"
#include "UI/TickBox.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/Platform/Process.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <system_error>

namespace Graphite {

	namespace {
		constexpr std::chrono::seconds kDirectoryCheckInterval{5};
		constexpr std::chrono::seconds kCacheSaveInterval{30};
		constexpr std::chrono::steady_clock::time_point kCheckImmediately{};
		constexpr std::chrono::steady_clock::time_point kNeverSaved{};

		constexpr std::size_t kTitleBytes = 200;

		constexpr float kFilterWidthInFontSizes = 16.0f;
		const ImVec4 kLaunchProblemColour(0.9f, 0.5f, 0.4f, 1.0f);

		// WARNING: Liveness comes from the fingerprint, which the index refreshes whenever the transcript changes.
		// Asking the filesystem instead would cost a stat per row per frame.
		bool IsLive(const SessionSummary& summary)
		{
			if (summary.Print.WriteTicks == 0)
			{
				return false;
			}

			const std::filesystem::file_time_type written{
				std::filesystem::file_time_type::duration{summary.Print.WriteTicks}};
			return std::filesystem::file_time_type::clock::now() - written < kLiveWindow;
		}

		std::string DurationOf(const SessionSummary& summary)
		{
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> start = ParseTimestamp(summary.StartedAt);
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> end = ParseTimestamp(summary.EndedAt);
			return start && end ? FormatDuration(*end - *start) : std::string{};
		}
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

	SessionsPanel::SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings,
								 SessionWindows& windows, LaunchClaude launch)
		: Core::Panel("sessions", "Claude Sessions", "ai", ICON_MDI_ROBOT_OUTLINE, StartsAs::Hidden),
		  m_Workspace(workspace), m_Settings(settings), m_Windows(windows), m_Launch(std::move(launch))
	{
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

	bool SessionsPanel::VisibleRowPrecedes(const VisibleRow& earlier, const VisibleRow& later)
	{
		if (earlier.Folder != later.Folder)
		{
			return earlier.Folder < later.Folder;
		}

		return earlier.Summary->EndedAt > later.Summary->EndedAt;
	}

	std::vector<SessionsPanel::VisibleRow> SessionsPanel::VisibleSessions() const
	{
		const bool limitToWorkspace = m_WorkspaceOnly && m_Workspace.IsOpen();
		const std::string filter = Core::Lowered(m_Filter);

		std::vector<VisibleRow> visible;

		for (const auto& [file, summary] : m_Sessions)
		{
			const bool isLiveOrOpen = IsLive(summary) || m_Windows.IsOpen(summary.Id);

			if (!ShowsInSessionList(summary, isLiveOrOpen))
			{
				continue;
			}

			if (limitToWorkspace && !IsWithinDirectory(summary.Cwd, m_Workspace.GetRoot()))
			{
				continue;
			}

			if (!filter.empty() && !Core::LoweredTextContains(summary.DisplayTitle(), filter))
			{
				continue;
			}

			const std::filesystem::path::string_type folderKey =
				limitToWorkspace ? std::filesystem::path::string_type{} : DirectoryKey(summary.Cwd);
			visible.push_back({&summary, folderKey});
		}

		std::sort(visible.begin(), visible.end(), VisibleRowPrecedes);

		return visible;
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

		if (!m_Launch("Claude", NewSessionCommand(program, id), WorkspaceRootOrCurrentDirectory()))
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_Windows.OpenPending(id, "New Claude session");

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

		if (!m_Launch("Claude", ResumeCommand(program, summary.Id), directory))
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_NextDirectoryCheck = kCheckImmediately;
		m_Windows.Open(summary);
	}

	void SessionsPanel::RenderToolbar()
	{
		ImGui::BeginDisabled(!m_Workspace.IsOpen());
		DrawTickBox("This workspace only", &m_WorkspaceOnly);
		ImGui::EndDisabled();

		ImGui::SameLine();

		DrawFilterField("##sessionFilter", m_Filter,
						{.Hint = "Filter by title", .Width = ImGui::GetFontSize() * kFilterWidthInFontSizes});

		ImGui::SameLine();

		if (ImGui::Button(ICON_MDI_PLUS "  New Claude session"))
		{
			StartNewSession();
		}

		if (m_LaunchProblem.has_value())
		{
			ImGui::TextColored(kLaunchProblemColour, "%s", m_LaunchProblem->c_str());
		}

		if (m_InitialScan)
		{
			ImGui::TextDisabled("%s", ScanProgressText(m_ScanProgress.Scanned, m_ScanProgress.Total).c_str());
		}
	}

	// WARNING: The title is drawn over an id-only row, not as a Selectable's label, which a "##" in a session's title
	// would cut short. It stands the row end's inset in from the list's edge, inside the row's inset highlight, and
	// the dim line under it keeps to the title's indent.
	SessionRowAction DrawSessionRow(const SessionRowText& text)
	{
		constexpr bool kNeverSelected = false;
		SessionRowAction action = SessionRowAction::None;

		const ImVec2 rowStart = ImGui::GetCursorPos();

		if (ListRowSelectable("##session", kNeverSelected))
		{
			action = SessionRowAction::Open;
		}

		if (Core::BeginRightClickPopup("##sessionMenu"))
		{
			if (ImGui::MenuItem("Continue in terminal"))
			{
				action = SessionRowAction::Continue;
			}

			ImGui::EndPopup();
		}

		ImGui::Indent(ListRowEndInset());
		ImGui::SetCursorPos(ImVec2(rowStart.x + ListRowEndInset(), ListRowTextTop(rowStart.y)));
		ImGui::TextUnformatted(FitToWidth(text.Title, ListRowEndPosX() - ImGui::GetCursorPosX()).c_str());

		ImGui::Indent();
		ImGui::TextDisabled("%s", text.Detail.c_str());

		if (!text.DetailHint.empty() && ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", text.DetailHint.c_str());
		}

		ImGui::Unindent();
		ImGui::Unindent(ListRowEndInset());

		return action;
	}

	void SessionsPanel::RenderRow(const SessionSummary& summary)
	{
		ImGui::PushID(Core::ToUtf8(summary.File).c_str());

		const Pricing& pricing = m_Settings.GetPricing();
		const CostResult mainCost = pricing.CostOf(summary.Main);
		CostResult totalCost = mainCost;
		const CostResult subagentCost = pricing.CostOf(summary.Subagents);
		totalCost += subagentCost;

		SessionRowText text{.Title = (IsLive(summary) ? std::format("{} ", kLiveSessionMark) : std::string{}) +
									 SingleLineLabel(summary.DisplayTitle(), kTitleBytes),
							.Detail = FormatCost(totalCost),
							.DetailHint = UnknownCostHint(totalCost)};

		if (summary.SubagentCount > 0)
		{
			text.Detail += " (subagents " + FormatCost(subagentCost) + ")";
		}
		if (const std::string duration = DurationOf(summary); !duration.empty())
		{
			text.Detail += "  " + duration;
		}
		if (const std::string ended = m_Clock.Format(summary.EndedAt, ClockStyle::Date); !ended.empty())
		{
			text.Detail += "  " + ended;
		}

		switch (DrawSessionRow(text))
		{
		case SessionRowAction::Open:
			m_Windows.Open(summary);
			break;
		case SessionRowAction::Continue:
			ContinueSession(summary);
			break;
		case SessionRowAction::None:
			break;
		}

		ImGui::PopID();
	}

	void SessionsPanel::RenderList()
	{
		const std::vector<VisibleRow> visible = VisibleSessions();
		const bool limitedToWorkspace = m_WorkspaceOnly && m_Workspace.IsOpen();

		if (visible.empty())
		{
			Core::EmptyState(EmptyListMessage(m_InitialScan, m_ScanProgress.Scanned, m_ScanProgress.Total,
											  limitedToWorkspace, m_Filter)
								 .c_str());
			return;
		}

		const bool grouped = !limitedToWorkspace;

		if (!ImGui::BeginChild("##sessionList"))
		{
			ImGui::EndChild();
			return;
		}

		const Pricing& pricing = m_Settings.GetPricing();
		const std::filesystem::path::string_type* group = nullptr;
		bool groupOpen = true;

		for (std::size_t rowIndex = 0; rowIndex < visible.size(); ++rowIndex)
		{
			const SessionSummary& summary = *visible[rowIndex].Summary;
			const std::filesystem::path::string_type& folder = visible[rowIndex].Folder;

			if (grouped && (group == nullptr || *group != folder))
			{
				group = &folder;

				CostResult total;
				for (std::size_t groupMemberIndex = rowIndex;
					 groupMemberIndex < visible.size() && visible[groupMemberIndex].Folder == folder; ++groupMemberIndex)
				{
					for (const UsageByModel* usage :
						 {&visible[groupMemberIndex].Summary->Main, &visible[groupMemberIndex].Summary->Subagents})
					{
						total += pricing.CostOf(*usage);
					}
				}

				// WARNING: The id must never be empty. A transcript with no cwd leaves Cwd empty, and an empty id
				// hashes to the child window's own id and asserts. The id is the group's key, not the text shown,
				// which changes as the group's sessions do.
				const std::string cwdText = Core::ToUtf8(summary.Cwd);
				const std::string label = cwdText.empty() ? "(unknown folder)" : cwdText;
				const std::string cost = FormatCost(total);
				const std::string groupId = "group-" + Core::ToUtf8(std::filesystem::path{folder});
				groupOpen = ChevronTreeNode(groupId.c_str(), ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen,
											label.c_str(), cost.c_str());

				if (const std::string hint = UnknownCostHint(total); !hint.empty() && ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", hint.c_str());
				}
			}

			if (!grouped || groupOpen)
			{
				RenderRow(summary);
			}
		}

		ImGui::EndChild();
	}

	void SessionsPanel::OnImGuiRender()
	{
		// WARNING: The toolbar is drawn even when there is a problem; "New Claude session" is how a machine
		// with no transcripts gets its first one.
		RenderToolbar();
		ImGui::Separator();

		if (m_Problem.has_value())
		{
			ImGui::TextWrapped("%s", m_Problem->c_str());
			return;
		}

		RenderList();
	}
}
