#include "Core/Base/Base.h"

#include "Panels/SessionsPanel.h"

#include "Panels/SessionWindows.h"
#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionText.h"
#include "Sessions/SummaryCache.h"
#include "Workspace/Workspace.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Log.h"
#include "Core/Base/Uuid.h"
#include "Core/Platform/Process.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>

namespace Graphite {

	namespace {
		// A session whose transcript moved this recently is taken to be running.
		constexpr std::chrono::minutes kLiveWindow{2};

		bool IsLive(const SessionSummary& summary)
		{
			std::error_code status;
			const auto written = std::filesystem::last_write_time(summary.File, status);
			return !status && std::filesystem::file_time_type::clock::now() - written < kLiveWindow;
		}

		std::string DurationOf(const SessionSummary& summary)
		{
			const auto start = ParseTimestamp(summary.StartedAt);
			const auto end = ParseTimestamp(summary.EndedAt);
			return start && end ? FormatDuration(*end - *start) : std::string{};
		}

		std::string PathText(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return {reinterpret_cast<const char*>(text.data()), text.size()};
		}
	}

	SessionsPanel::SessionsPanel(const Workspace& workspace, const ClaudeSettings& settings,
								 SessionWindows& windows, LaunchClaude launch)
		: Core::Panel("sessions", "Claude Sessions", "ai", ICON_MDI_ROBOT_OUTLINE),
		  m_Workspace(workspace), m_Settings(settings), m_Windows(windows), m_Launch(std::move(launch))
	{
	}

	void SessionsPanel::StartIndexing()
	{
		m_Started = true;

		const Core::Result<std::filesystem::path> home = Core::HomeDirectory();

		if (!home)
		{
			m_Problem = "Cannot find your home directory: " + home.GetError().Message;
			return;
		}

		m_Projects = ClaudeProjectsDirectory(Core::EnvironmentPath(L"CLAUDE_CONFIG_DIR"), home.GetValue());

		std::error_code status;

		if (!std::filesystem::is_directory(m_Projects, status))
		{
			m_Problem = "No Claude transcripts at " + PathText(m_Projects);
			return;
		}

		if (const Core::Result<std::filesystem::path> directory = Core::ExecutableDirectory())
			m_CacheFile = directory.GetValue() / "sessions-cache.json";

		std::vector<SessionSummary> cached = m_CacheFile.empty() ? std::vector<SessionSummary>{}
																 : LoadSummaryCache(m_CacheFile);

		// Shown before the scan confirms them: a cached summary is what the session was when
		// last seen, and the scan replaces whichever changed.
		for (const SessionSummary& summary : cached)
			m_Sessions[summary.File] = summary;

		m_Index.Start(m_Projects, std::move(cached));
		m_CacheDirty = true;

		if (Core::Result<Core::Scope<Core::DirectoryWatcher>> watcher = Core::DirectoryWatcher::Start(m_Projects))
			m_Watcher = std::move(watcher).GetValue();
		else
			APP_ERROR("Cannot watch {}: {}", PathText(m_Projects), watcher.GetError().Message);
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
			return;

		Core::DirectoryWatcher::Changes changes = m_Watcher->TakeSettled();

		if (changes.Overflowed)
		{
			// Something was missed; a refresh of everything is cheap because of the fingerprints.
			m_Index.Refresh({m_Projects});
			m_Windows.NotifyChanged({m_Projects});
			return;
		}

		if (changes.Directories.empty())
			return;

		m_Windows.NotifyChanged(changes.Directories);
		m_Index.Refresh(std::move(changes.Directories));
	}

	void SessionsPanel::SaveCacheWhenSettled()
	{
		if (!m_CacheDirty || m_CacheFile.empty() || m_Index.IsScanning())
			return;

		std::vector<SessionSummary> summaries;
		summaries.reserve(m_Sessions.size());

		for (const auto& [file, summary] : m_Sessions)
			summaries.push_back(summary);

		if (!SaveSummaryCache(m_CacheFile, summaries))
			APP_ERROR("Cannot write {}", PathText(m_CacheFile));

		m_CacheDirty = false;
	}

	void SessionsPanel::OnUpdate(Core::Timestep ts)
	{
		(void)ts;

		// Started on the first update rather than in the constructor, so an application that
		// never shows this panel still pays nothing until it is drawn once.
		if (!m_Started)
		{
			if (!WasDrawn())
				return;

			StartIndexing();
		}

		TakeFromIndex();
		TakeFromWatcher();
		SaveCacheWhenSettled();
		m_Windows.Update();
	}

	std::vector<const SessionSummary*> SessionsPanel::VisibleSessions() const
	{
		const bool limitToWorkspace = m_WorkspaceOnly && m_Workspace.IsOpen();
		const std::string filter = Core::Lowered(m_Filter);

		std::vector<const SessionSummary*> visible;

		for (const auto& [file, summary] : m_Sessions)
		{
			if (limitToWorkspace && !IsWithinDirectory(summary.Cwd, m_Workspace.GetRoot()))
				continue;

			if (!filter.empty() && Core::Lowered(summary.DisplayTitle()).find(filter) == std::string::npos)
				continue;

			visible.push_back(&summary);
		}

		// Grouped by where each was started when every project is shown, newest first within.
		std::sort(visible.begin(), visible.end(), [limitToWorkspace](const SessionSummary* a, const SessionSummary* b)
		{
			if (!limitToWorkspace && a->Cwd != b->Cwd)
				return a->Cwd < b->Cwd;

			return a->EndedAt > b->EndedAt;
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

		const std::string id = Core::Uuid::Generate().ToString();

		if (!m_Launch("Claude", NewSessionCommand(program, id)))
		{
			m_LaunchProblem = "Claude did not start. The console has the reason.";
			return;
		}

		m_LaunchProblem.reset();
		m_Windows.OpenPending(id, "New Claude session");
	}

	void SessionsPanel::ContinueSession(const SessionSummary& summary)
	{
		const std::filesystem::path program = m_Settings.ResolveProgram();

		if (program.empty())
		{
			m_LaunchProblem = "Claude was not found. Set its path under Settings > Claude.";
			return;
		}

		if (m_Launch("Claude", ResumeCommand(program, summary.Id)))
			m_Windows.Open(summary);
	}

	void SessionsPanel::RenderToolbar()
	{
		ImGui::BeginDisabled(!m_Workspace.IsOpen());
		ImGui::Checkbox("This workspace only", &m_WorkspaceOnly);
		ImGui::EndDisabled();

		ImGui::SameLine();

		char buffer[256] = {};
		m_Filter.copy(buffer, sizeof(buffer) - 1);
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);

		if (ImGui::InputTextWithHint("##sessionFilter", "Filter by title", buffer, sizeof(buffer)))
			m_Filter = buffer;

		ImGui::SameLine();

		if (ImGui::Button(ICON_MDI_PLUS "  New Claude session"))
			StartNewSession();

		if (m_LaunchProblem.has_value())
			ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.4f, 1.0f), "%s", m_LaunchProblem->c_str());

		if (m_Index.IsScanning())
			ImGui::TextDisabled("Reading transcripts...");
	}

	void SessionsPanel::RenderRow(const SessionSummary& summary)
	{
		ImGui::PushID(PathText(summary.File).c_str());

		const Pricing& pricing = m_Settings.GetPricing();
		const CostResult main = pricing.CostOf(summary.Main);
		CostResult all = main;
		const CostResult subagents = pricing.CostOf(summary.Subagents);
		all.Dollars += subagents.Dollars;
		all.UnknownModels.insert(all.UnknownModels.end(), subagents.UnknownModels.begin(),
								 subagents.UnknownModels.end());

		const std::string title = (IsLive(summary) ? "\xe2\x97\x8f " : "") + summary.DisplayTitle();

		if (ImGui::Selectable(title.c_str()))
			m_Windows.Open(summary);

		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Continue in terminal"))
				ContinueSession(summary);

			ImGui::EndPopup();
		}

		std::string detail = FormatCost(all);

		if (summary.SubagentCount > 0)
			detail += " (subagents " + FormatCost(subagents) + ")";
		if (const std::string duration = DurationOf(summary); !duration.empty())
			detail += "  " + duration;
		detail += "  " + summary.EndedAt.substr(0, 10);

		ImGui::Indent();
		ImGui::TextDisabled("%s", detail.c_str());
		ImGui::Unindent();

		ImGui::PopID();
	}

	void SessionsPanel::RenderList()
	{
		const std::vector<const SessionSummary*> visible = VisibleSessions();

		if (visible.empty())
		{
			ImGui::TextDisabled(m_Index.IsScanning() ? "" : "No sessions.");
			return;
		}

		const bool grouped = !(m_WorkspaceOnly && m_Workspace.IsOpen());

		if (!ImGui::BeginChild("##sessionList"))
		{
			ImGui::EndChild();
			return;
		}

		const Pricing& pricing = m_Settings.GetPricing();
		const std::filesystem::path* group = nullptr;
		bool groupOpen = true;

		for (std::size_t index = 0; index < visible.size(); ++index)
		{
			const SessionSummary& summary = *visible[index];

			if (grouped && (group == nullptr || *group != summary.Cwd))
			{
				group = &summary.Cwd;

				CostResult total;
				for (std::size_t other = index; other < visible.size() && visible[other]->Cwd == summary.Cwd; ++other)
				{
					for (const UsageByModel* usage : {&visible[other]->Main, &visible[other]->Subagents})
					{
						const CostResult part = pricing.CostOf(*usage);
						total.Dollars += part.Dollars;
						total.UnknownModels.insert(total.UnknownModels.end(), part.UnknownModels.begin(),
												   part.UnknownModels.end());
					}
				}

				// A transcript with no cwd on any record leaves Cwd empty; the id suffix must
				// still be non-empty, or ImGui hashes it to the same id as the child window
				// itself and asserts ("Cannot have an empty ID at the root of a window").
				const std::string cwdText = PathText(summary.Cwd);
				const std::string label = cwdText.empty() ? "(unknown folder)" : cwdText;
				const std::string header = label + "  " + FormatCost(total) + "###group-" + cwdText;
				groupOpen = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			}

			if (!grouped || groupOpen)
				RenderRow(summary);
		}

		ImGui::EndChild();
	}

	void SessionsPanel::OnImGuiRender()
	{
		if (m_Problem.has_value())
		{
			ImGui::TextWrapped("%s", m_Problem->c_str());
			return;
		}

		RenderToolbar();
		ImGui::Separator();
		RenderList();
	}
}
