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

	class SentTextView
	{
	public:
		std::optional<std::size_t> Draw(const ApiRequest& request, std::uint64_t version,
										std::optional<std::size_t> selected, std::optional<std::size_t> hovered,
										const SessionPalette& palette, Core::EventBus& events);

		void ScrollTo(std::size_t segment) { m_PendingScroll = segment; }

	private:
		SentTextLayout m_Layout;
		std::string m_BuiltKey;
		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::size_t m_BuiltColumns = 0;
		bool m_Built = false;

		std::optional<std::size_t> m_PendingScroll;

		std::optional<std::size_t> m_MenuSegment;
	};
}
