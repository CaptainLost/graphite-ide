#pragma once

#include "Panels/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"
#include "Theme/SessionPalette.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	// One API request as the session views show it, folded to a line or opened to what it sent
	// and returned. Shared by the Requests tab and the table under a usage line, so a request
	// reads the same wherever it is listed.

	// "#42 18:12:11 · opus-5-5 · ctx 663.9k +2.3k · 99% cache · 1.1k out · $0.15 · 6.2 s".
	// A part the request does not carry — a timestamp that does not parse, no usage, no wait
	// before it — is left out rather than shown as zero.
	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock);

	// The folded row through the draw list, from `at` to `right`: RequestLine, the mark of a
	// rebuilt cache, how the response stopped, and the tools the request used. Draws no item:
	// the caller puts the Selectable or Dummy under it that makes the row one line tall.
	void DrawRequestLine(ImVec2 at, float right, const ApiRequest& request, const Pricing& pricing,
						 const SessionPalette& palette, LocalClock& clock);

	// What the request sent, what it returned, and the tools the model had then, one line an
	// item, laid out as ordinary items below the cursor.
	void DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette);
}
