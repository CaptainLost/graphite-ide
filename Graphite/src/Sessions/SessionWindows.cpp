#include "Sessions/SessionWindows.h"

#include "Core/Base/Base.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "UI/RememberedPlace.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/TabContextMenu.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		// A tab shows the start of a title; the whole of it is in the session's list row.
		constexpr std::size_t kTabTitleBytes = 60;

		double Megabytes(std::uint64_t bytes)
		{
			return static_cast<double>(bytes) / (1024.0 * 1024.0);
		}

		// While the transcript's first pass is still being read this is all a tab shows: a large
		// transcript takes a while, in a Debug build especially, and a conversation that grows
		// chunk by chunk under the reader's eyes is worse to watch than a bar that fills. Once
		// the transcript is in, only a quiet line remains while the subagents are still read.
		void DrawProgress(const LiveTranscript::LoadProgress& progress)
		{
			if (!progress.FirstPassDone)
			{
				const float fraction =
					progress.Total > 0 ? static_cast<float>(static_cast<double>(progress.Bytes) /
															static_cast<double>(progress.Total))
									   : 0.0f;
				const std::string label =
					std::format("{:.1f} / {:.1f} MB", Megabytes(progress.Bytes), Megabytes(progress.Total));

				ImGui::TextDisabled("Loading transcript\xe2\x80\xa6");
				ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
			}
			else if (!progress.SubagentsWalked)
			{
				ImGui::TextDisabled("Reading subagents\xe2\x80\xa6 %zu / %zu", progress.SubagentsDone,
									progress.SubagentsTotal);
			}
		}

		// A subagent's views draw under an id of their own, so they share no scroll or table
		// state with the session's, and each open has a serial of its own, so no open inherits the
		// scroll position of whichever agent ImGui last drew under "agent". The tab bar is outside
		// it: its selection carries across. Closed before EndTabItem, which pops the tab's own id.
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

		// Written to within the live window, by the transcript's own clock rather than a stat per frame.
		bool WrittenLately(const Session& session)
		{
			const auto at = ParseTimestamp(session.GetLastWrittenAt());
			return at.has_value() && std::chrono::system_clock::now() - *at < kLiveWindow;
		}
	}

	SessionWindows::SessionWindows(Core::EventBus& events, const ClaudeSettings& settings)
		: m_Events(events), m_Settings(settings)
	{
	}

	bool SessionWindows::IsOpen(const std::string& id) const
	{
		return std::any_of(m_Open.begin(), m_Open.end(),
						   [&id](const Core::Scope<Entry>& entry) { return SameSessionId(entry->Id, id); });
	}

	SessionWindows::Entry* SessionWindows::Find(const std::string& id)
	{
		const auto found = std::find_if(m_Open.begin(), m_Open.end(),
										[&id](const Core::Scope<Entry>& entry) { return SameSessionId(entry->Id, id); });
		return found == m_Open.end() ? nullptr : found->get();
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
			// A pending "New Claude session" tab reaches here with no reader yet when the user
			// clicks it in the list before the index has delivered a summary for it: start
			// reading now rather than waiting for UpdateSummary, which may be a poll away.
			if (!existing->Main.Live)
			{
				Attach(*existing, summary);
			}

			// Opened from the folder that is open now, so it belongs to this one, even if its
			// tab was carried over from the one before.
			existing->CarriedOver = false;
			existing->Focus = true;
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
			existing->Focus = true;
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
			// Still open from the folder this one replaced: written to that folder already.
			if (entry->CarriedOver)
			{
				continue;
			}

			// A view still waiting to be selected is reported as the one shown: the tab bar has
			// not been drawn yet, so what it last selected says nothing about the reader.
			sessions.push_back(
				SessionTabState{entry->Id, entry->Title, entry->RestoredTab.Reported(entry->Selected)});
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
			// Already open - a folder opened over another can share a session with it - and the
			// window someone is using keeps its own view. It belongs to this folder too now.
			if (Entry* existing = Find(session.Id))
			{
				existing->CarriedOver = false;
				continue;
			}

			auto entry = Core::CreateScope<Entry>();
			entry->Id = session.Id;
			entry->Title = session.Title;
			entry->RestoredTab.Restore(session.Tab);
			entry->Restored = true;
			entry->Looking = true;

			// A restored window does not pull itself forward: the one the reader was looking at
			// is whichever its dock node remembers as selected.
			entry->Focus = false;

			m_Open.push_back(std::move(entry));
		}
	}

	bool SessionWindows::IsLookingForSessions() const
	{
		return std::any_of(m_Open.begin(), m_Open.end(),
						   [](const Core::Scope<Entry>& entry) { return entry->Looking; });
	}

	void SessionWindows::SettleRestored(const std::function<const SessionSummary*(const std::string& id)>& find)
	{
		for (std::size_t index = 0; index < m_Open.size();)
		{
			Entry& entry = *m_Open[index];

			if (!entry.Looking)
			{
				++index;
				continue;
			}

			if (const SessionSummary* summary = find(entry.Id))
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

			// The session's own project directory, or anything inside its folder — which is
			// every directory the watcher reports that could hold something of this session.
			const bool mine = std::any_of(directories.begin(), directories.end(),
										  [&file](const std::filesystem::path& directory) {
											  return directory == file.parent_path() ||
													 IsWithinDirectory(directory, file.parent_path() / file.stem()) ||
													 IsWithinDirectory(file, directory);
										  });

			// A subagent's transcript lies inside its session's folder, so whatever moves the
			// session can move it.
			if (mine)
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

			// The session keeps being read while a subagent is shown, so going back finds it current.
			if (entry->Agent)
			{
				Take(entry->Agent->Views);
			}
		}
	}

	bool SessionWindows::DrawBreadcrumb(const Entry& entry)
	{
		const SubagentInfo& info = entry.Agent->Info;

		bool back = ImGui::SmallButton(ICON_MDI_ARROW_LEFT "##agentBack");
		ImGui::SameLine();

		// The session's title goes back too: it is where the reader came from.
		back = ImGui::SmallButton((SingleLineLabel(entry.Title, kTabTitleBytes) + "##agentSession").c_str()) || back;
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_MDI_CHEVRON_RIGHT);
		ImGui::SameLine();

		std::string name = info.Description.empty() ? "agent " + info.AgentId : info.Description;
		if (!info.AgentType.empty())
		{
			name += " \xc2\xb7 " + info.AgentType;
		}

		ImGui::TextUnformatted(name.c_str());

		if (WrittenLately(entry.Agent->Views.Data))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("\xe2\x97\x8f live");
		}

		// Esc only while this window has the keyboard and nothing in it is being edited, so it
		// never takes Esc from a field elsewhere. Mouse button 3 is the side "back" button.
		// ImGui does not mark Esc as used when it acts on it: NavUpdateCancelRequest (imgui.cpp, run
		// in NewFrame before this) reads it with no owner and then, in this order, clears the active
		// item, leaves the menu layer, exits a child window, or closes the topmost popup. By the time
		// this runs the state that told those apart has changed, so entry.EscBusy holds what it was
		// at the end of last frame: Esc goes back only when no item was active, no popup open and
		// the nav not on the menu layer. Exiting a child is left out on purpose: it is invisible to
		// the reader (a click in the conversation leaves the nav focus there), so the same Esc both
		// leaves the child, as ImGui does in NewFrame, and goes back.
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		back = back || (focused && !entry.EscBusy && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
		back = back || (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(3));

		return back;
	}

	void SessionWindows::OpenAgent(Entry& entry, const std::string& toolUseId)
	{
		const auto found = entry.Main.Links.find(toolUseId);
		if (!entry.Main.Live || found == entry.Main.Links.end())
		{
			return;
		}

		auto agent = Core::CreateScope<OpenedAgent>();
		agent->Views.Serial = m_NextAgentSerial++;

		// Not the previous agent's: the first frame of this one has no last frame of its own.
		entry.EscBusy = false;
		agent->Info = found->second;
		agent->Views.Live =
			Core::CreateScope<LiveTranscript>(SubagentTranscriptPath(entry.Main.Live->GetFile(), found->second.AgentId));
		agent->Views.Live->Poll();
		entry.Agent = std::move(agent);
	}

	void SessionWindows::DrawTabs(Entry& entry, const SessionPalette& palette, OpenedAgent* agent)
	{
		TranscriptViews& views = agent != nullptr ? agent->Views : entry.Main;

		// A subagent has no summary of its own: the Cost tab then leaves out the split between
		// the session and its subagents.
		const std::optional<SessionSummary> noSummary;
		const std::optional<SessionSummary>& summary = agent != nullptr ? noSummary : entry.Summary;

		if (!ImGui::BeginTabBar("##sessionTabs"))
		{
			return;
		}

		// A restored view is selected once, on the first frame the tab bar is drawn.
		const auto restoredFlag = [&entry](SessionTab tab) {
			return entry.RestoredTab.ShouldSelect(tab) ? ImGuiTabItemFlags_SetSelected
													   : ImGuiTabItemFlags_None;
		};

		if (ImGui::BeginTabItem("Conversation", nullptr, restoredFlag(SessionTab::Conversation)))
		{
			entry.Selected = SessionTab::Conversation;

			{
				const AgentIdScope idScope{agent != nullptr, views.Serial};

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
						[&views](std::size_t request) { views.PendingRequest = request; }};

					// Subagents of a subagent are not followed: there is one level to go into.
					if (agent == nullptr)
					{
						context.Links = &views.Links;
						context.OpenAgent = [&entry](const std::string& toolUseId) { entry.PendingAgent = toolUseId; };
					}

					views.Conversation.Draw(views.Data, context, m_Events);
				}
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Cost", nullptr, restoredFlag(SessionTab::Cost)))
		{
			entry.Selected = SessionTab::Cost;

			{
				const AgentIdScope idScope{agent != nullptr, views.Serial};

				// The same rule as the conversation: a cost summed over half a transcript
				// is a wrong number, not a partial one.
				const LiveTranscript::LoadProgress progress = views.Live->GetProgress();

				// A request clicked on its chart is shown in the Requests tab, as one picked
				// in the conversation is.
				if (progress.FirstPassDone)
				{
					if (const std::optional<std::size_t> clicked =
							views.Cost.Draw(views.Data, summary, views.Timeline, m_Settings.GetPricing(),
											views.Requests.GetSelected(), palette))
					{
						views.PendingRequest = clicked;
					}
				}
				else
				{
					DrawProgress(progress);
				}
			}

			ImGui::EndTabItem();
		}

		// A request asked for from the conversation or the Cost tab, drawn above, selects
		// this tab once; the tab bar moves to it on the next frame. SetSelected on every
		// frame would pin the reader to it.
		const ImGuiTabItemFlags requestsFlags =
			views.PendingRequest ? ImGuiTabItemFlags_SetSelected : restoredFlag(SessionTab::Requests);
		const bool requestsOpen = ImGui::BeginTabItem("Requests", nullptr, requestsFlags);

		// Revealed whether or not the tab is drawn this frame: the view keeps it for its
		// next draw, which is the frame the selection takes effect.
		if (const std::optional<std::size_t> pending = std::exchange(views.PendingRequest, std::nullopt))
		{
			views.Requests.Reveal(*pending, views.Data.GetGeneration());
		}

		if (requestsOpen)
		{
			entry.Selected = SessionTab::Requests;

			{
				const AgentIdScope idScope{agent != nullptr, views.Serial};

				// The same rule again: a request's context delta and the tools it had are
				// only right once every record before it has been read.
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
				ICON_MDI_ROBOT_OUTLINE "  " + SingleLineLabel(entry.Title, kTabTitleBytes) + "###session-" + entry.Id;

			// Docked only on the frame this identity is new, as DiffWindows does: every frame
			// would fight a reader who dragged the tab elsewhere. A window reopened from the last
			// launch goes back to where it was instead, which imgui.ini already carries - unless
			// that place is gone, and then it goes where a new one would.
			bool restored = false;

			if (std::exchange(entry.Fresh, false))
			{
				restored = std::exchange(entry.Restored, false);

				if (centreNode != 0 && (!restored || !HasRememberedPlace(name)))
				{
					ImGui::DockBuilderDockWindow(name.c_str(), centreNode);
				}
			}

			if (std::exchange(entry.Focus, false))
			{
				ImGui::SetNextWindowFocus();
			}

			bool open = true;

			// Restored windows must not take focus on the frame they appear: ImGui focuses every
			// window then, focusing a docked window selects its tab, and so the last one drawn
			// would take the front of its node from whatever imgui.ini remembers there.
			const ImGuiWindowFlags windowFlags =
				restored ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			// A tab's menu asked for this one last frame: the same as its close button.
			if (closeRequests.Take(name))
			{
				open = false;
			}

			Core::DrawTabContextMenu(closeRequests);

			if (visible)
			{
				if (!entry.Main.Live)
				{
					// A restored window is waiting for the index to find a transcript that already
					// exists; a new one for Claude to start writing it.
					ImGui::TextDisabled("%s", entry.Looking
												  ? "Looking for this session\xe2\x80\xa6"
												  : "Waiting for Claude to write the session's first lines...");
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
						// Read once and found nothing: deleted since the session listed it, or not
						// written to yet. A write brings the tabs back.
						ImGui::TextDisabled("This agent's transcript is empty or gone.");
					}
					else
					{
						DrawTabs(entry, palette, entry.Agent.get());
					}

					if (entry.Agent)
					{
						const ImGuiContext& g = *ImGui::GetCurrentContext();
						entry.EscBusy = g.ActiveId != 0 || g.NavLayer != ImGuiNavLayer_Main || g.OpenPopupStack.Size > 0;
					}

					if (const std::optional<std::string> toolUseId = std::exchange(entry.PendingAgent, std::nullopt))
					{
						OpenAgent(entry, *toolUseId);
					}
				}
			}

			// Begin() returning false still needs its End().
			ImGui::End();

			if (open)
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
