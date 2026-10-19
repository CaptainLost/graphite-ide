#include "Sessions/SessionWindows.h"

#include "Core/Base/Base.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TabContextMenu.h"
#include "Core/ImGui/TextButton.h"

#include <IconsLucide.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kTabTitleBytes = 60;
		constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
		constexpr float kFillAvailableWidth = -1.0f;
		constexpr ImGuiMouseButton kMouseBackButton = 3;

		double Megabytes(std::uint64_t bytes)
		{
			return static_cast<double>(bytes) / kBytesPerMegabyte;
		}

		float LoadedFraction(const LiveTranscript::LoadProgress& progress)
		{
			if (progress.Total == 0)
			{
				return 0.0f;
			}

			const double loadedRatio = static_cast<double>(progress.Bytes) / static_cast<double>(progress.Total);
			return static_cast<float>(loadedRatio);
		}

		void DrawProgress(const LiveTranscript::LoadProgress& progress)
		{
			if (!progress.FirstPassDone)
			{
				const float fraction = LoadedFraction(progress);
				const std::string label =
					std::format("{:.1f} / {:.1f} MB", Megabytes(progress.Bytes), Megabytes(progress.Total));

				ImGui::TextDisabled("Loading transcript…");
				ImGui::ProgressBar(fraction, ImVec2(kFillAvailableWidth, 0.0f), label.c_str());
			}
			else if (!progress.SubagentsWalked)
			{
				Core::LoadingLine(std::format("Reading subagents {} / {}", progress.SubagentsDone, progress.SubagentsTotal).c_str());
			}
		}

		// WARNING: The tab bar must stay outside this scope so its selection carries across agents,
		// and the scope must close before EndTabItem, which pops the tab's own id.
		class AgentIdScope
		{
		public:
			explicit AgentIdScope(bool active, std::uint64_t serial) : m_Active(active)
			{
				if (m_Active)
				{
					ImGui::PushID("agent");
					ImGui::PushID(static_cast<int>(serial));
				}
			}

			~AgentIdScope()
			{
				if (m_Active)
				{
					ImGui::PopID();
					ImGui::PopID();
				}
			}

			AgentIdScope(const AgentIdScope&) = delete;
			AgentIdScope& operator=(const AgentIdScope&) = delete;

		private:
			bool m_Active;
		};

		bool WrittenLately(const Session& session)
		{
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> writtenAt =
				ParseTimestamp(session.GetLastWrittenAt());
			return writtenAt.has_value() && std::chrono::system_clock::now() - *writtenAt < kLiveWindow;
		}

		ImGuiTabItemFlags SelectionFlag(const RestoredTabSelection& restoredTab, SessionTab tab)
		{
			return restoredTab.ShouldSelect(tab) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
		}

		bool ConcernsSessionFile(const std::filesystem::path& changedDirectory,
								 const std::filesystem::path& sessionFile)
		{
			const std::filesystem::path sessionDirectory = sessionFile.parent_path();
			const std::filesystem::path subagentDirectory = sessionDirectory / sessionFile.stem();

			return changedDirectory == sessionDirectory || IsWithinDirectory(changedDirectory, subagentDirectory) ||
				   IsWithinDirectory(sessionFile, changedDirectory);
		}

		bool ConcernsAnyDirectory(const std::vector<std::filesystem::path>& changedDirectories,
								  const std::filesystem::path& sessionFile)
		{
			for (const std::filesystem::path& changedDirectory : changedDirectories)
			{
				if (ConcernsSessionFile(changedDirectory, sessionFile))
				{
					return true;
				}
			}

			return false;
		}
	}

	SessionWindows::SessionWindows(Core::EventBus& events, const ClaudeSettings& settings)
		: m_Events(events), m_Settings(settings)
	{
	}

	bool SessionWindows::IsOpen(const std::string& id) const
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (SameSessionId(entry->Id, id))
			{
				return true;
			}
		}

		return false;
	}

	SessionWindows::Entry* SessionWindows::Find(const std::string& id)
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (SameSessionId(entry->Id, id))
			{
				return entry.get();
			}
		}

		return nullptr;
	}

	void SessionWindows::Attach(Entry& entry, const SessionSummary& summary)
	{
		entry.Title = summary.DisplayTitle();
		entry.Summary = summary;
		entry.Looking = false;

		if (!entry.Main.Live)
		{
			entry.Main.Live = Core::CreateScope<LiveTranscript>(summary.File);
			entry.Main.Live->Poll();
		}
	}

	void SessionWindows::Open(const SessionSummary& summary)
	{
		if (Entry* existing = Find(summary.Id))
		{
			const bool isPendingWithoutReader = !existing->Main.Live;

			if (isPendingWithoutReader)
			{
				Attach(*existing, summary);
			}

			existing->CarriedOver = false;
			existing->Window.Focus = true;
			return;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Id = summary.Id;
		Attach(*entry, summary);
		m_Open.push_back(std::move(entry));
	}

	void SessionWindows::OpenPending(const std::string& id, std::string title)
	{
		if (Entry* existing = Find(id))
		{
			existing->CarriedOver = false;
			existing->Window.Focus = true;
			return;
		}

		auto entry = Core::CreateScope<Entry>();
		entry->Id = id;
		entry->Title = std::move(title);
		m_Open.push_back(std::move(entry));
	}

	void SessionWindows::UpdateSummary(const SessionSummary& summary)
	{
		if (Entry* entry = Find(summary.Id))
		{
			Attach(*entry, summary);
		}
	}

	std::vector<SessionTabState> SessionWindows::CaptureState() const
	{
		std::vector<SessionTabState> sessions;
		sessions.reserve(m_Open.size());

		for (const Core::Scope<Entry>& entry : m_Open)
		{
			const bool isWrittenToPreviousFolder = entry->CarriedOver;

			if (isWrittenToPreviousFolder)
			{
				continue;
			}

			const SessionTab reportedTab = entry->RestoredTab.Reported(entry->Selected);
			sessions.push_back(SessionTabState{entry->Id, entry->Title, reportedTab});
		}

		return sessions;
	}

	void SessionWindows::CarryOverOpenSessions()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			entry->CarriedOver = true;
		}
	}

	void SessionWindows::RestoreState(const std::vector<SessionTabState>& sessions)
	{
		for (const SessionTabState& session : sessions)
		{
			if (Entry* existing = Find(session.Id))
			{
				existing->CarriedOver = false;
				continue;
			}

			auto entry = Core::CreateScope<Entry>();
			entry->Id = session.Id;
			entry->Title = session.Title;
			entry->RestoredTab.Restore(session.Tab);
			entry->Window.Restored = true;
			entry->Looking = true;
			entry->Window.Focus = false;

			m_Open.push_back(std::move(entry));
		}
	}

	bool SessionWindows::IsLookingForSessions() const
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (entry->Looking)
			{
				return true;
			}
		}

		return false;
	}

	void SessionWindows::SettleRestored(
		const std::function<const SessionSummary*(const std::string& id)>& findSummary)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = *m_Open[index];

			if (!entry.Looking)
			{
				++index;
				continue;
			}

			if (const SessionSummary* summary = findSummary(entry.Id))
			{
				Attach(entry, *summary);
				++index;
				continue;
			}

			APP_INFO("Not reopening Claude session '{}' ({}): the index has no transcript for it", entry.Title,
					 entry.Id);
			m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}

	void SessionWindows::NotifyChanged(const std::vector<std::filesystem::path>& directories)
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (!entry->Main.Live)
			{
				continue;
			}

			const std::filesystem::path& file = entry->Main.Live->GetFile();
			const bool concernsThisSession = ConcernsAnyDirectory(directories, file);

			if (concernsThisSession)
			{
				entry->Main.Live->Poll();

				if (entry->Agent)
				{
					entry->Agent->Views.Live->Poll();
				}
			}
		}
	}

	void SessionWindows::Take(TranscriptViews& views)
	{
		if (!views.Live)
		{
			return;
		}

		views.Live->Take(views.Data, views.Timeline);
		if (views.Live->TakeSubagents(views.Subagents, views.Links))
		{
			++views.SubagentsVersion;
		}
	}

	void SessionWindows::Update()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			Take(entry->Main);

			if (entry->Agent)
			{
				Take(entry->Agent->Views);
			}
		}
	}

	bool SessionWindows::DrawBreadcrumb(const Entry& entry)
	{
		const SubagentInfo& agentInfo = entry.Agent->Info;

		constexpr bool kNotEngaged = false;

		bool backRequested = Core::DrawIconButton("##agentBack", ICON_LC_ARROW_LEFT, kNotEngaged, "Back to the session",
												  {.Fit = Core::IconButtonFit::Line});
		ImGui::SameLine();

		backRequested =
			Core::SmallTextButton((SingleLineLabel(entry.Title, kTabTitleBytes) + "##agentSession").c_str()) ||
			backRequested;
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_LC_CHEVRON_RIGHT);
		ImGui::SameLine();

		std::string name = agentInfo.Description.empty() ? "agent " + agentInfo.AgentId : agentInfo.Description;
		if (!agentInfo.AgentType.empty())
		{
			name += kMiddleDotSeparator;
			name += agentInfo.AgentType;
		}

		ImGui::TextUnformatted(name.c_str());

		if (WrittenLately(entry.Agent->Views.Data))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", std::format("{} live", kLiveSessionMark).c_str());
		}

		// WARNING: ImGui does not mark Esc as used when it acts on it, and by the time this runs the
		// state that told its uses apart has changed, so entry.EscBusy holds that state from the end
		// of last frame. Leaving a child window is left out of it on purpose: it is invisible to the
		// reader, so the same Esc both leaves the child and goes back.
		const bool escapeRepeats = false;
		const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		backRequested = backRequested ||
						(windowFocused && !entry.EscBusy && ImGui::IsKeyPressed(ImGuiKey_Escape, escapeRepeats));
		backRequested = backRequested || (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
										  ImGui::IsMouseClicked(kMouseBackButton));

		return backRequested;
	}

	void SessionWindows::OpenAgent(Entry& entry, const std::string& toolUseId)
	{
		const SubagentLinks::const_iterator linkedAgent = entry.Main.Links.find(toolUseId);
		if (!entry.Main.Live || linkedAgent == entry.Main.Links.end())
		{
			return;
		}

		auto agent = Core::CreateScope<OpenedAgent>();
		agent->Views.Serial = m_NextAgentSerial++;

		entry.EscBusy = false;
		const auto& [linkedToolUseId, linkedInfo] = *linkedAgent;
		agent->Info = linkedInfo;
		agent->Views.Live = Core::CreateScope<LiveTranscript>(
			SubagentTranscriptPath(entry.Main.Live->GetFile(), linkedInfo.AgentId));
		agent->Views.Live->Poll();
		entry.Agent = std::move(agent);
	}

	void SessionWindows::DrawTabs(Entry& entry, const SessionPalette& palette, OpenedAgent* agent)
	{
		TranscriptViews& views = agent != nullptr ? agent->Views : entry.Main;

		const bool isSubagentView = agent != nullptr;
		const std::optional<SessionSummary> noSummary;
		const std::optional<SessionSummary>& summary = isSubagentView ? noSummary : entry.Summary;

		if (!ImGui::BeginTabBar("##sessionTabs"))
		{
			return;
		}

		if (ImGui::BeginTabItem("Conversation", nullptr, SelectionFlag(entry.RestoredTab, SessionTab::Conversation)))
		{
			entry.Selected = SessionTab::Conversation;

			{
				const AgentIdScope idScope{isSubagentView, views.Serial};

				if (const std::size_t skipped = views.Live->GetSkipped(); skipped > 0)
				{
					ImGui::TextDisabled("%zu lines could not be read and were skipped.", skipped);
				}

				const LiveTranscript::LoadProgress progress = views.Live->GetProgress();
				DrawProgress(progress);

				if (progress.FirstPassDone)
				{
					ConversationContext context{
						m_Settings.GetPricing(), views.Subagents, palette, views.SubagentsVersion, &views.Timeline,
						[&views](std::size_t requestIndex) { views.PendingRequest = requestIndex; }};

					if (!isSubagentView)
					{
						context.Links = &views.Links;
						context.OpenAgent = [&entry](const std::string& toolUseId) { entry.PendingAgent = toolUseId; };
					}

					views.Conversation.Draw(views.Data, context, m_Events);
				}
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Cost", nullptr, SelectionFlag(entry.RestoredTab, SessionTab::Cost)))
		{
			entry.Selected = SessionTab::Cost;

			{
				const AgentIdScope idScope{isSubagentView, views.Serial};

				const LiveTranscript::LoadProgress progress = views.Live->GetProgress();

				if (progress.FirstPassDone)
				{
					if (const std::optional<std::size_t> clickedRequest =
							views.Cost.Draw(views.Data, summary, views.Timeline, m_Settings.GetPricing(),
											views.Requests.GetSelected(), palette))
					{
						views.PendingRequest = clickedRequest;
					}
				}
				else
				{
					DrawProgress(progress);
				}
			}

			ImGui::EndTabItem();
		}

		const ImGuiTabItemFlags requestsFlags = views.PendingRequest
													? ImGuiTabItemFlags_SetSelected
													: SelectionFlag(entry.RestoredTab, SessionTab::Requests);
		const bool requestsOpen = ImGui::BeginTabItem("Requests", nullptr, requestsFlags);

		if (const std::optional<std::size_t> pending = std::exchange(views.PendingRequest, std::nullopt))
		{
			views.Requests.Reveal(*pending, views.Data.GetGeneration());
		}

		if (requestsOpen)
		{
			entry.Selected = SessionTab::Requests;

			{
				const AgentIdScope idScope{isSubagentView, views.Serial};

				const LiveTranscript::LoadProgress progress = views.Live->GetProgress();

				if (progress.FirstPassDone)
				{
					views.Requests.Draw(
						views.Timeline, views.Data,
						ConversationContext{m_Settings.GetPricing(), views.Subagents, palette, views.SubagentsVersion},
						m_Events);
				}
				else
				{
					DrawProgress(progress);
				}
			}

			ImGui::EndTabItem();
		}

		entry.RestoredTab.AfterTabBar(entry.Selected);

		ImGui::EndTabBar();
	}

	void SessionWindows::Render(unsigned int centreNode, const SessionPalette& palette,
								Core::TabCloseRequests& closeRequests)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = *m_Open[index];
			const std::string name =
				ICON_LC_BOT "  " + SingleLineLabel(entry.Title, kTabTitleBytes) + "###session-" + entry.Id;

			const TabWindowFrame frame = BeginTabWindow(name, entry.Window, DockInto(centreNode), closeRequests);

			Core::DrawTabContextMenu(closeRequests);

			if (frame.Visible)
			{
				if (!entry.Main.Live)
				{
					if (entry.Looking)
					{
						Core::LoadingLine("Looking for this session");
					}
					else
					{
						ImGui::TextDisabled("Waiting for Claude to write the session's first lines...");
					}
				}
				else
				{
					if (entry.Agent && DrawBreadcrumb(entry))
					{
						entry.Agent.reset();
					}

					if (!entry.Agent)
					{
						DrawTabs(entry, palette, nullptr);
					}
					else if (const LiveTranscript::LoadProgress progress = entry.Agent->Views.Live->GetProgress();
							 progress.FirstPassDone && progress.Total == 0)
					{
						ImGui::TextDisabled("This agent's transcript is empty or gone.");
					}
					else
					{
						DrawTabs(entry, palette, entry.Agent.get());
					}

					if (entry.Agent)
					{
						const ImGuiContext& imguiContext = *ImGui::GetCurrentContext();
						const bool hasActiveItem = imguiContext.ActiveId != 0;
						const bool isNavigatingMenuLayer = imguiContext.NavLayer != ImGuiNavLayer_Main;
						const bool hasOpenPopup = imguiContext.OpenPopupStack.Size > 0;
						entry.EscBusy = hasActiveItem || isNavigatingMenuLayer || hasOpenPopup;
					}

					if (const std::optional<std::string> toolUseId = std::exchange(entry.PendingAgent, std::nullopt))
					{
						OpenAgent(entry, *toolUseId);
					}
				}
			}

			ImGui::End();

			if (frame.Open)
			{
				++index;
			}
			else
			{
				m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
			}
		}
	}
}
