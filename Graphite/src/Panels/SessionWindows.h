#pragma once

#include "Panels/ConversationView.h"
#include "Panels/CostView.h"
#include "Sessions/LiveTranscript.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"

#include "Core/Base/Base.h"
#include "Core/Events/EventBus.h"

#include <filesystem>
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
		void NotifyChanged(const std::vector<std::filesystem::path>& directories);

		// Applies what each tab's reader delivered. UI thread, once per frame.
		void Update();

		void Render(unsigned int centreNode);

	private:
		struct Entry
		{
			std::string Id;
			std::string Title;
			std::optional<SessionSummary> Summary;
			Core::Scope<LiveTranscript> Live;
			Session Data;
			ConversationView Conversation;
			bool Fresh = true;
			bool Focus = true;
		};

		Entry* Find(const std::string& id);
		void Attach(Entry& entry, const SessionSummary& summary);

		Core::EventBus& m_Events;
		const ClaudeSettings& m_Settings;

		// Scoped, so opening or closing a tab never moves the others. A window's ImGui id does
		// not depend on where its entry lives: it comes from "###session-<id>", which is what
		// keeps a tab's dock place as the list around it changes.
		std::vector<Core::Scope<Entry>> m_Open;
	};
}
