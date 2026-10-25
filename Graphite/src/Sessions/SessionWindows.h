#pragma once

#include "Sessions/Conversation/ConversationView.h"
#include "Sessions/Charts/CostView.h"
#include "Sessions/Requests/RequestsView.h"
#include "Persistence/PersistedState.h"
#include "Persistence/RestoredTabSelection.h"
#include "Sessions/Transcript/LiveTranscript.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "Terminal/CommandTerminal.h"
#include "Theme/SessionPalette.h"
#include "UI/TabWindow.h"

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

	class SessionWindows
	{
	public:
		SessionWindows(Core::EventBus& events, const ClaudeSettings& settings);

		void Open(const SessionSummary& summary);

		void OpenPending(const std::string& id, std::string title, Core::Ref<CommandTerminal> terminal);

		void UpdateSummary(const SessionSummary& summary);

		bool IsOpen(const std::string& id) const;

		std::vector<SessionTabState> CaptureState() const;

		void CarryOverOpenSessions();

		void RestoreState(const std::vector<SessionTabState>& sessions);

		bool IsLookingForSessions() const;

		void SettleRestored(const std::function<const SessionSummary*(const std::string& id)>& findSummary);
		void NotifyChanged(const std::vector<std::filesystem::path>& directories);

		void Update();

		void Render(unsigned int centreNode, const SessionPalette& palette,
					Core::TabCloseRequests& closeRequests);

	private:
		struct TranscriptViews
		{
			Core::Scope<LiveTranscript> Live;
			Session Data;
			SubagentUsage Subagents;

			SubagentLinks Links;

			std::uint64_t SubagentsVersion = 0;
			ConversationView Conversation;
			CostView Cost;

			RequestTimeline Timeline;
			RequestsView Requests;

			std::optional<std::size_t> PendingRequest;

			std::uint64_t Serial = 0;
		};

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

			Core::Scope<OpenedAgent> Agent;

			std::optional<std::string> PendingAgent;

			bool EscBusy = false;
			TabWindowState Window{.Focus = true};

			SessionTab Selected = SessionTab::Conversation;

			RestoredTabSelection RestoredTab;

			bool Looking = false;

			Core::Ref<CommandTerminal> Terminal;

			bool CarriedOver = false;
		};

		Entry* Find(const std::string& id);
		void Attach(Entry& entry, const SessionSummary& summary);

		static void DrawWithoutTranscript(Entry& entry);

		static void Take(TranscriptViews& views);

		void DrawTabs(Entry& entry, const SessionPalette& palette, OpenedAgent* agent);

		static bool DrawBreadcrumb(const Entry& entry);

		void OpenAgent(Entry& entry, const std::string& toolUseId);

		Core::EventBus& m_Events;
		const ClaudeSettings& m_Settings;

		// WARNING: A window's ImGui id comes from "###session-<id>", never from where its entry
		// lives in this list, which is what keeps a tab's dock place as the list changes.
		std::vector<Core::Scope<Entry>> m_Open;

		std::uint64_t m_NextAgentSerial = 1;
	};
}
