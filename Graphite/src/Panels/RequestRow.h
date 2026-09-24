#pragma once

#include "Panels/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"
#include "Theme/SessionPalette.h"

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// One API request as the session views show it, folded to a line or opened to what it sent
	// and returned. Shared by the Requests tab and the table under a usage line, so a request
	// reads the same wherever it is listed.

	// The pieces a request's line and its row in the Requests table are made of, so the two
	// read the same.

	// "opus-5-5" for "claude-opus-5-5".
	std::string ShortModelName(std::string_view model);

	// "+2.3k", or "−2.3k" with U+2212 MINUS SIGN for a context that shrank.
	std::string SignedTokens(std::int64_t tokens);

	// "850 ms", "6.2 s", or FormatDuration past a minute.
	std::string WaitText(std::chrono::milliseconds waited);

	const char* StopGlyph(std::string_view reason);

	// "#42 18:12:11 · opus-5-5 · ctx 663.9k +2.3k · 99% cache · 1.1k out · $0.15 · 6.2 s".
	// A part the request does not carry — a timestamp that does not parse, no usage, no wait
	// before it — is left out rather than shown as zero. `time` is how the timestamp is written:
	// the time alone in a list, the whole date where the request stands on its own.
	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock,
							ClockStyle time = ClockStyle::Time);

	// The folded row through the draw list, from `at` to `right`: RequestLine, the mark of a
	// rebuilt cache, how the response stopped, and the tools the request used. Draws no item:
	// the caller puts the Selectable or Dummy under it that makes the row one line tall.
	void DrawRequestLine(ImVec2 at, float right, const ApiRequest& request, const Pricing& pricing,
						 const SessionPalette& palette, LocalClock& clock);

	// How an item is drawn wherever it is listed: its icon, in its colour.
	struct ItemLook
	{
		const char* Icon = nullptr;
		ImU32 Colour = 0;
	};

	ItemLook LookOf(const SentItem& item, const SessionPalette& palette);
	ItemLook LookOf(const ResponseItem& item, const SessionPalette& palette);

	// "3.5 KB  ~880 tok": a sent item's size and its share of the context, with "≈ " in
	// front when its text is a reconstruction.
	std::string SentColumn(const SentItem& item);

	// What the reader did to the list this frame, by the item's position in Sent then
	// Response — the same index SegmentText takes.
	struct DetailsPick
	{
		std::optional<std::size_t> Clicked;
		std::optional<std::size_t> Hovered;
	};

	// What the request sent, what it returned, and the tools the model had then, one
	// selectable line an item, laid out as ordinary items below the cursor. `selected` is drawn
	// selected; `reveal` is scrolled into view when it is not.
	DetailsPick DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette,
								   std::optional<std::size_t> selected, std::optional<std::size_t> reveal);
}
