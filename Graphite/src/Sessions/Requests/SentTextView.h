#pragma once

#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Requests/SentTextLayout.h"
#include "Theme/SessionPalette.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Core {

	class EventBus;

}

namespace Graphite {

	// The plain text a request's items put in front of the model, and what it wrote back: a
	// header per item, then its text wrapped to the width, in the order the list beside it
	// shows the items. Draws into the current window, drawing only the rows in view; the
	// caller opens the child it scrolls in.
	class SentTextView
	{
	public:
		// `version` is the timeline's, so the rows are laid out again when the request's
		// items change under the same key. Returns the item whose header or text was clicked.
		// `events` is where the context menu's Copy is confirmed.
		std::optional<std::size_t> Draw(const ApiRequest& request, std::uint64_t version,
										std::optional<std::size_t> selected, std::optional<std::size_t> hovered,
										const SessionPalette& palette, Core::EventBus& events);

		// Puts the item's header at the top on the next draw.
		void ScrollTo(std::size_t segment) { m_PendingScroll = segment; }

	private:
		SentTextLayout m_Layout;
		std::string m_BuiltKey;
		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::size_t m_BuiltColumns = 0;
		bool m_Built = false;

		std::optional<std::size_t> m_PendingScroll;

		// The item the context menu was opened over, if it was opened over one.
		std::optional<std::size_t> m_MenuSegment;
	};
}
