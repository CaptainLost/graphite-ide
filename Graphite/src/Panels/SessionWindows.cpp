#include "Core/Base/Base.h"

#include "Panels/SessionWindows.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/SessionText.h"
#include "UI/RememberedPlace.h"

#include "Core/Base/Log.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
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
	}

	SessionWindows::SessionWindows(Core::EventBus& events, const ClaudeSettings& settings)
		: m_Events(events), m_Settings(settings)
	{
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

		if (!entry.Live)
		{
			entry.Live = Core::CreateScope<LiveTranscript>(summary.File);
			entry.Live->Poll();
		}
	}

	void SessionWindows::Open(const SessionSummary& summary)
	{
		if (Entry* existing = Find(summary.Id))
		{
			// A pending "New Claude session" tab reaches here with no reader yet when the user
			// clicks it in the list before the index has delivered a summary for it: start
			// reading now rather than waiting for UpdateSummary, which may be a poll away.
			if (!existing->Live)
				Attach(*existing, summary);

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
			Attach(*entry, summary);
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
			if (!entry->Live)
				continue;

			const std::filesystem::path& file = entry->Live->GetFile();

			// The session's own project directory, or anything inside its folder — which is
			// every directory the watcher reports that could hold something of this session.
			const bool mine = std::any_of(directories.begin(), directories.end(),
										  [&file](const std::filesystem::path& directory)
										  {
											  return directory == file.parent_path() ||
													 IsWithinDirectory(directory, file.parent_path() / file.stem()) ||
													 IsWithinDirectory(file, directory);
										  });

			if (mine)
				entry->Live->Poll();
		}
	}

	void SessionWindows::Update()
	{
		for (const Core::Scope<Entry>& entry : m_Open)
		{
			if (!entry->Live)
				continue;

			entry->Live->Take(entry->Data, entry->Timeline);
			if (entry->Live->TakeSubagents(entry->Subagents))
				++entry->SubagentsVersion;
		}
	}

	void SessionWindows::Render(unsigned int centreNode, const SessionPalette& palette)
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
			if (std::exchange(entry.Fresh, false))
			{
				const bool restored = std::exchange(entry.Restored, false);

				if (centreNode != 0 && (!restored || !HasRememberedPlace(name)))
				{
					ImGui::DockBuilderDockWindow(name.c_str(), centreNode);
				}
			}

			if (std::exchange(entry.Focus, false))
				ImGui::SetNextWindowFocus();

			bool open = true;

			if (ImGui::Begin(name.c_str(), &open))
			{
				if (!entry.Live)
				{
					// A restored window is waiting for the index to find a transcript that already
					// exists; a new one for Claude to start writing it.
					ImGui::TextDisabled("%s", entry.Looking
												  ? "Looking for this session\xe2\x80\xa6"
												  : "Waiting for Claude to write the session's first lines...");
				}
				else if (ImGui::BeginTabBar("##sessionTabs"))
				{
					// A restored view is selected once, on the first frame the tab bar is drawn.
					const auto restoredFlag = [&entry](SessionTab tab)
					{
						return entry.RestoredTab.ShouldSelect(tab) ? ImGuiTabItemFlags_SetSelected
																	: ImGuiTabItemFlags_None;
					};

					if (ImGui::BeginTabItem("Conversation", nullptr, restoredFlag(SessionTab::Conversation)))
					{
						entry.Selected = SessionTab::Conversation;

						if (const std::size_t skipped = entry.Live->GetSkipped(); skipped > 0)
							ImGui::TextDisabled("%zu lines could not be read and were skipped.", skipped);

						const LiveTranscript::LoadProgress progress = entry.Live->GetProgress();
						DrawProgress(progress);

						if (progress.FirstPassDone)
						{
							const ConversationContext context{
								m_Settings.GetPricing(), entry.Subagents, palette, entry.SubagentsVersion, &entry.Timeline,
								[&entry](std::size_t request) { entry.PendingRequest = request; }};

							entry.Conversation.Draw(entry.Data, context, m_Events);
						}

						ImGui::EndTabItem();
					}

					if (ImGui::BeginTabItem("Cost", nullptr, restoredFlag(SessionTab::Cost)))
					{
						entry.Selected = SessionTab::Cost;

						// The same rule as the conversation: a cost summed over half a transcript
						// is a wrong number, not a partial one.
						const LiveTranscript::LoadProgress progress = entry.Live->GetProgress();

						// A request clicked on its chart is shown in the Requests tab, as one picked
						// in the conversation is.
						if (progress.FirstPassDone)
						{
							if (const std::optional<std::size_t> clicked =
									entry.Cost.Draw(entry.Data, entry.Summary, entry.Timeline, m_Settings.GetPricing(),
													entry.Requests.GetSelected(), palette))
								entry.PendingRequest = clicked;
						}
						else
						{
							DrawProgress(progress);
						}

						ImGui::EndTabItem();
					}

					// A request asked for from the conversation or the Cost tab, drawn above, selects
					// this tab once; the tab bar moves to it on the next frame. SetSelected on every
					// frame would pin the reader to it.
					const ImGuiTabItemFlags requestsFlags =
						entry.PendingRequest ? ImGuiTabItemFlags_SetSelected : restoredFlag(SessionTab::Requests);
					const bool requestsOpen = ImGui::BeginTabItem("Requests", nullptr, requestsFlags);

					// Revealed whether or not the tab is drawn this frame: the view keeps it for its
					// next draw, which is the frame the selection takes effect.
					if (const std::optional<std::size_t> pending = std::exchange(entry.PendingRequest, std::nullopt))
						entry.Requests.Reveal(*pending, entry.Data.GetGeneration());

					if (requestsOpen)
					{
						entry.Selected = SessionTab::Requests;

						// The same rule again: a request's context delta and the tools it had are
						// only right once every record before it has been read.
						const LiveTranscript::LoadProgress progress = entry.Live->GetProgress();

						if (progress.FirstPassDone)
						{
							entry.Requests.Draw(
								entry.Timeline, entry.Data,
								ConversationContext{m_Settings.GetPricing(), entry.Subagents, palette, entry.SubagentsVersion});
						}
						else
						{
							DrawProgress(progress);
						}

						ImGui::EndTabItem();
					}

					entry.RestoredTab.AfterTabBar(entry.Selected);

					ImGui::EndTabBar();
				}
			}

			// Begin() returning false still needs its End().
			ImGui::End();

			if (open)
				++index;
			else
				m_Open.erase(m_Open.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}
}
