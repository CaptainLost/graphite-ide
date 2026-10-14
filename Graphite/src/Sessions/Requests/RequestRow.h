#pragma once

#include "Sessions/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Theme/SessionPalette.h"

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	std::string SignedTokens(std::int64_t tokens);

	std::string WaitText(std::chrono::milliseconds waited);

	const char* StopGlyph(std::string_view reason);

	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock,
							ClockStyle clockStyle = ClockStyle::Time);

	void DrawRequestLine(ImVec2 drawPosition, float right, const ApiRequest& request, const Pricing& pricing,
						 const SessionPalette& palette, LocalClock& clock);

	inline constexpr std::size_t kLongestLabelBytes = 400;

	struct ItemLook
	{
		const char* Icon = nullptr;
		ImU32 Colour = 0;
	};

	ItemLook LookOf(const SentItem& item, const SessionPalette& palette);
	ItemLook LookOf(const ResponseItem& item, const SessionPalette& palette);

	std::string SentColumn(const SentItem& item);

	struct DetailsPick
	{
		std::optional<std::size_t> Clicked;
		std::optional<std::size_t> Hovered;
	};

	DetailsPick DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette,
								   std::optional<std::size_t> selected, std::optional<std::size_t> reveal);
}
