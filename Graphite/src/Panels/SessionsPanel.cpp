#include "Panels/SessionsPanel.h"

#include "Core/Base/Base.h"

#include "Panels/SessionWindows.h"
#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionText.h"
#include "Sessions/SummaryCache.h"
#include "Settings/UserDirectories.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TextInput.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/Platform/Process.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace Graphite {

	namespace {
		// How often a missing projects directory is looked for, and how often the cache may be
		// rewritten while summaries keep arriving.
		constexpr std::chrono::seconds kDirectoryCheckInterval{5};
		constexpr std::chrono::seconds kCacheSaveInterval{30};

		// A title longer than this is cut: the row has the rest of its line for the details.
		constexpr std::size_t kTitleBytes = 200;

		// From the fingerprint rather than the file: the index refreshes it whenever the
		// transcript changes, and asking the filesystem instead is a stat per row per frame.
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
			const auto start = ParseTimestamp(summary.StartedAt);
			const auto end = ParseTimestamp(summary.EndedAt);
			return start && end ? FormatDuration(*end - *start) : std::string{};
		}
	}

	std::filesystem::path SessionsPanel::WorkspaceRoot() const
	{
		if (m_Workspace.IsOpen())
		{
			return m_Workspace.GetRoot();
		}

		// The overload that cannot throw: a working directory deleted from under the process
		// makes the other one raise out of a frame (ADR 0004). Empty then, which callers take
		// as "no directory" and let the child process start in its own default.
		std::error_code ignored;
		return std::filesystem::current_path(ignored);
	}

	SessionsPanel::SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings,
								 SessionWindows& windows, LaunchClaude launch)
		: Core::Panel("sessions", "Claude Sessions", "ai", ICON_MDI_ROBOT_OUTLINE, false),
		  m_Workspace(workspace), m_Settings(settings), m_Windows(windows), m_Launch(std::move(launch))
	{
	}

	SessionsPanel::~SessionsPanel()
	{
		// Whatever arrived since the last throttled save. The fingerprints make a cache that is
		// a little behind harmless, but a live session's last turns are worth keeping.
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

		const auto now = std::chrono::steady_clock::now();

		if (now < m_NextDirectoryCheck)
		{
			return;
		}

		m_NextDirectoryCheck = now + kDirectoryCheckInterval;

		// Claude Code creates the directory with the first session it writes, so a machine
		// that has never run it gets one soon after "New Claude session" is first used.
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

		// Shown before the scan confirms them: a cached summary is what the session was when
		// last seen, and the scan replaces whichever changed.
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
			// Something was missed; a refresh of everything is cheap because of the fingerprints.
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
		if (!m_CacheDirty || m_Index.IsScanning())
		{
			return;
		}

		// The first save is not held back: it is the one that records the initial scan.
		const auto now = std::chrono::steady_clock::now();

		if (m_LastCacheSave != std::chrono::steady_clock::time_point{} && now - m_LastCacheSave < kCacheSaveInterval)
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
		// Started on the first update rather than in the constructor, so an application that
		// never shows this panel still pays nothing until it is drawn once.
		if (!m_Started)
		{
			// A session window restored from the last launch needs the index to find its
			// transcript, whether or not this list has been shown yet.
			if (!HasBeenDrawn() && !m_Windows.IsLookingForSessions())
			{
				return;
			}

			m_Started = true;
			FindProjectsDirectory();
		}

		StartIndexingWhenPresent();

		if (m_Indexing)
		{
			// Asked before taking: the worker delivers every summary before it stops counting as
			// scanning, so an index idle here has nothing left to deliver once the take below is
			// done. Asked after, it could have finished in between and left its last summaries
			// for the next frame - after the scan had already been declared complete.
			const bool scanning = m_Index.IsScanning();

			TakeFromIndex();
			TakeFromWatcher();

			if (m_InitialScan && !scanning)
			{
				m_InitialScan = false;
			}

			// Every session there is is known once the first full scan is in, so a restored
			// window still looking for its session now either finds it or never will.
			if (!m_InitialScan && m_Windows.IsLookingForSessions())
			{
				m_Windows.SettleRestored([this](const std::string& id) -> const SessionSummary* {
					for (const auto& [file, summary] : m_Sessions)
					{
						if (SameSessionId(summary.Id, id))
						{
							return &summary;
						}
					}

					return nullptr;
				});
			}

			SaveCacheWhenSettled();
		}

		m_Windows.Update();
	}

	std::vector<SessionsPanel::VisibleRow> SessionsPanel::VisibleSessions() const
	{
		const bool limitToWorkspace = m_WorkspaceOnly && m_Workspace.IsOpen();
		const std::string filter = Core::Lowered(m_Filter);

		std::vector<VisibleRow> visible;

		for (const auto& [file, summary] : m_Sessions)
		{
			if (limitToWorkspace && !IsWithinDirectory(summary.Cwd, m_Workspace.GetRoot()))
			{
				continue;
			}

			if (!filter.empty() && Core::Lowered(summary.DisplayTitle()).find(filter) == std::string::npos)
			{
				continue;
			}

			// Keyed as IsWithinDirectory compares, so two spellings of one folder - as Windows
			// sees it, case and a trailing separator aside - make one group rather than two.
			visible.push_back({&summary, limitToWorkspace ? std::filesystem::path::string_type{} : DirectoryKey(summary.Cwd)});
		}

		// Grouped by where each was started when every project is shown, newest first within.
		std::sort(visible.begin(), visible.end(), [](const VisibleRow& a, const VisibleRow& b) {
			if (a.Folder != b.Folder)
			{
				return a.Folder < b.Folder;
			}

			return a.Summary->EndedAt > b.Summary->EndedAt;
		});

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

		// Lower case, as Claude Code writes the id into the transcript's file name: the pending
		// tab is matched to the index's summary by it.
		const std::string id = Core::Lowered(Core::Uuid::Generate().ToString());

		if (!m_Launch("Claude", NewSessionCommand(program, id), WorkspaceRoot()))
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_Windows.OpenPending(id, "New Claude session");

		// The first session on this machine is what creates the projects directory.
		m_NextDirectoryCheck = {};
	}

	void SessionsPanel::ContinueSession(const SessionSummary& summary)
	{
		const std::filesystem::path program = m_Settings.ResolveProgram();

		if (program.empty())
		{
			m_LaunchProblem = "Claude was not found. Set its path under Settings > Claude.";
			return;
		}

		// Claude Code looks up the transcript to resume under the project of the directory it
		// is started in, so this must be the session's own folder - the workspace root would
		// resume the wrong project, or fail to find one at all, whenever "This workspace only"
		// is unticked or the session was started in a subfolder of the workspace.
		std::error_code status;
		const std::filesystem::path directory =
			!summary.Cwd.empty() && std::filesystem::is_directory(summary.Cwd, status) ? summary.Cwd
																					   : WorkspaceRoot();

		if (!m_Launch("Claude", ResumeCommand(program, summary.Id), directory))
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_NextDirectoryCheck = {};
		m_Windows.Open(summary);
	}

	void SessionsPanel::RenderToolbar()
	{
		ImGui::BeginDisabled(!m_Workspace.IsOpen());
		ImGui::Checkbox("This workspace only", &m_WorkspaceOnly);
		ImGui::EndDisabled();

		ImGui::SameLine();

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
		Core::InputTextWithHint("##sessionFilter", "Filter by title", m_Filter);

		ImGui::SameLine();

		if (ImGui::Button(ICON_MDI_PLUS "  New Claude session"))
		{
			StartNewSession();
		}

		if (m_LaunchProblem.has_value())
		{
			ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", m_LaunchProblem->c_str());
		}

		if (m_InitialScan)
		{
			ImGui::TextDisabled("Reading transcripts...");
		}
	}

	void SessionsPanel::RenderRow(const SessionSummary& summary)
	{
		ImGui::PushID(Core::ToUtf8(summary.File).c_str());

		const Pricing& pricing = m_Settings.GetPricing();
		const CostResult main = pricing.CostOf(summary.Main);
		CostResult all = main;
		const CostResult subagents = pricing.CostOf(summary.Subagents);
		all += subagents;

		const std::string title =
			(IsLive(summary) ? "\xe2\x97\x8f " : "") + SingleLineLabel(summary.DisplayTitle(), kTitleBytes);

		if (ImGui::Selectable(title.c_str()))
		{
			m_Windows.Open(summary);
		}

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Continue in terminal"))
			{
				ContinueSession(summary);
			}

			ImGui::EndPopup();
		}

		std::string detail = FormatCost(all);

		if (summary.SubagentCount > 0)
		{
			detail += " (subagents " + FormatCost(subagents) + ")";
		}
		if (const std::string duration = DurationOf(summary); !duration.empty())
		{
			detail += "  " + duration;
		}
		if (const std::string ended = m_Clock.Format(summary.EndedAt, ClockStyle::Date); !ended.empty())
		{
			detail += "  " + ended;
		}

		ImGui::Indent();
		ImGui::TextDisabled("%s", detail.c_str());
		ImGui::Unindent();

		ImGui::PopID();
	}

	void SessionsPanel::RenderList()
	{
		const std::vector<VisibleRow> visible = VisibleSessions();

		if (visible.empty())
		{
			Core::EmptyState(m_InitialScan ? "" : "No sessions.");
			return;
		}

		const bool grouped = !(m_WorkspaceOnly && m_Workspace.IsOpen());

		if (!ImGui::BeginChild("##sessionList"))
		{
			ImGui::EndChild();
			return;
		}

		const Pricing& pricing = m_Settings.GetPricing();
		const std::filesystem::path::string_type* group = nullptr;
		bool groupOpen = true;

		for (std::size_t index = 0; index < visible.size(); ++index)
		{
			const SessionSummary& summary = *visible[index].Summary;
			const std::filesystem::path::string_type& folder = visible[index].Folder;

			if (grouped && (group == nullptr || *group != folder))
			{
				group = &folder;

				CostResult total;
				for (std::size_t other = index; other < visible.size() && visible[other].Folder == folder; ++other)
				{
					for (const UsageByModel* usage : {&visible[other].Summary->Main, &visible[other].Summary->Subagents})
					{
						total += pricing.CostOf(*usage);
					}
				}

				// A transcript with no cwd on any record leaves Cwd empty; the id suffix must
				// still be non-empty, or ImGui hashes it to the same id as the child window
				// itself and asserts ("Cannot have an empty ID at the root of a window"). The id
				// is the group's key, not the spelling shown: that is whichever of its sessions
				// sorts first, and changes as they do.
				const std::string cwdText = Core::ToUtf8(summary.Cwd);
				const std::string label = cwdText.empty() ? "(unknown folder)" : cwdText;
				const std::string header =
					label + "  " + FormatCost(total) + "###group-" + Core::ToUtf8(std::filesystem::path{folder});
				groupOpen = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
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
		// The toolbar even when there is a problem: "New Claude session" is how a machine with
		// no transcripts yet gets its first one.
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
