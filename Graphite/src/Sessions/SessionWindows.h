#pragma once

#include "Sessions/Conversation/ConversationView.h"
#include "Sessions/Charts/CostView.h"
#include "Sessions/Requests/RequestsView.h"
#include "Persistence/PersistedState.h"
#include "Persistence/RestoredTabSelection.h"
#include "Sessions/LiveTranscript.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SubagentLinks.h"
#include "Sessions/ToolFacts.h"
#include "Theme/SessionPalette.h"

#include "Core/Base/Base.h"
#include "Core/Events/EventBus.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	class ClaudeSettings;

	// One central tab per open session, each following its transcript. The DiffWindows
	// pattern (ADR 0494): windows of our own, docked to the centre node the first frame each
	// identity appears, rather than panels, because there can be any number of them.
	class SessionWindows
	{
	public:
		SessionWindows(Core::EventBus& events, const ClaudeSettings& settings);

		void Open(const SessionSummary& summary);

		// A session started from the IDE: its id is known, its file is not until the index
		// reports a summary with that id.
		void OpenPending(const std::string& id, std::string title);

		void UpdateSummary(const SessionSummary& summary);

		// The open session windows, in tab order, each with the view it shows, for the next
		// launch. A window carried over from a folder this one replaced is left out (see
		// CarryOverOpenSessions).
		std::vector<SessionTabState> CaptureState() const;

		// Marks every open session window as carried over from the folder about to be replaced.
		// They stay open, but a folder's windows are written to that folder and never to
		// another, so CaptureState leaves these out. One stops being carried over when it closes,
		// when the Sessions panel opens it again, or when the new folder's restored state names
		// it too. The same rule DocumentWindows::CarryOverOpenDocuments keeps for file tabs.
		void CarryOverOpenSessions();

		// Reopens what CaptureState returned last time. A session's transcript is only known
		// once the index reports it, so each window starts out looking for it, under the title
		// it had, and settles once the index has either found it or finished without it (see
		// SettleRestored). None is brought forward, and each goes back to the place imgui.ini
		// remembers for it.
		void RestoreState(const std::vector<SessionTabState>& sessions);

		// Whether a restored window is still waiting for the index to find its session: the
		// Sessions panel starts the index for this even if the list itself has never been shown.
		bool IsLookingForSessions() const;

		// Called once the index has finished a full scan, with a way to find what it knows by
		// session id. A restored window still looking attaches to what `find` returns, or, when
		// that is nothing, closes: the transcript is gone, as a restored file tab whose file is
		// gone does not reopen.
		void SettleRestored(const std::function<const SessionSummary*(const std::string& id)>& find);
		void NotifyChanged(const std::vector<std::filesystem::path>& directories);

		// Applies what each tab's reader delivered. UI thread, once per frame.
		void Update();

		void Render(unsigned int centreNode, const SessionPalette& palette,
					Core::TabCloseRequests& closeRequests);

	private:
		// One transcript and the views drawn from it.
		struct TranscriptViews
		{
			Core::Scope<LiveTranscript> Live;
			Session Data;
			SubagentUsage Subagents;

			// The subagents this transcript started, by the id of the call that started each.
			SubagentLinks Links;

			// Counts the subagent maps taken, which the conversation's facts are keyed by.
			std::uint64_t SubagentsVersion = 0;
			ConversationView Conversation;
			CostView Cost;

			// The transcript cut into the API requests it made, fed from the same reads as Data.
			RequestTimeline Timeline;
			RequestsView Requests;

			// A request the conversation or the Cost tab asked to be shown, by its position in the
			// timeline: the Requests tab is selected and the request revealed on the next frame,
			// then cleared.
			std::optional<std::size_t> PendingRequest;

			// Tells one opened subagent from another in ImGui's ids. Zero for a session's own.
			std::uint64_t Serial = 0;
		};

		// A subagent's conversation shown in its session's window in place of the session's,
		// until the reader goes back. Not remembered across launches.
		struct OpenedAgent
		{
			SubagentInfo Info;
			TranscriptViews Views;
		};

		struct Entry
		{
			std::string Id;
			std::string Title;
			std::optional<SessionSummary> Summary;
			TranscriptViews Main;

			// The subagent being read in this window, if any. The session's own views are left
			// as they were while it is shown, so going back finds them where the reader left them.
			Core::Scope<OpenedAgent> Agent;

			// A subagent the conversation asked to show this frame, by the id of the call that
			// started it. Opened once the tab bar is done, never while it is being drawn.
			std::optional<std::string> PendingAgent;

			// Whether ImGui could have spent an Esc on something else at the end of last frame: an
			// item being edited, a popup, the nav on the menu layer. See DrawBreadcrumb.
			bool EscBusy = false;
			bool Fresh = true;
			bool Focus = true;

			// The view the tab bar showed last frame: whichever BeginTabItem returned true.
			SessionTab Selected = SessionTab::Conversation;

			// A view restored from the last launch, selected once on the first frame the tab bar
			// is drawn - which is not before the transcript is found - and reported as the
			// window's view until the tab bar has switched to it.
			RestoredTabSelection RestoredTab;

			// Reopened by RestoreState and not yet drawn: its first frame leaves it wherever
			// imgui.ini remembers it, when it remembers anywhere it can still go.
			bool Restored = false;

			// Reopened by RestoreState and not yet matched to a transcript. Cleared by Attach.
			bool Looking = false;

			// Still open from a folder another one replaced. See CarryOverOpenSessions.
			bool CarriedOver = false;
		};

		Entry* Find(const std::string& id);
		void Attach(Entry& entry, const SessionSummary& summary);

		// Applies what a transcript's reader delivered since the last call.
		static void Take(TranscriptViews& views);

		// The tab bar - Conversation, Cost, Requests - drawn from the session's transcript, or from
		// `agent`'s when it is given. The selection is the entry's whichever is drawn.
		void DrawTabs(Entry& entry, const SessionPalette& palette, OpenedAgent* agent);

		// The row above the tab bar while a subagent is shown. True when the reader asked to go
		// back to the session: its arrow or title, Esc, or the mouse's back button.
		static bool DrawBreadcrumb(const Entry& entry);

		// Starts reading the subagent started by call `toolUseId`, when the session knows it.
		void OpenAgent(Entry& entry, const std::string& toolUseId);

		Core::EventBus& m_Events;
		const ClaudeSettings& m_Settings;

		// Scoped, so opening or closing a tab never moves the others. A window's ImGui id does
		// not depend on where its entry lives: it comes from "###session-<id>", which is what
		// keeps a tab's dock place as the list around it changes.
		std::vector<Core::Scope<Entry>> m_Open;

		// The serial the next opened agent's views take. Only grows, so a reopened agent is a
		// new window state to ImGui as well.
		std::uint64_t m_NextAgentSerial = 1;
	};
}
