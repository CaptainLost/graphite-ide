#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardTips.h"

namespace Graphite {

	// WARNING: The tooltip a card is drawn in: sunk below the tiles and edged in colours.Frame, so it stands apart from
	// them. Pair with EndTipCard when it returns true.
	bool BeginTipCard(const DashboardColours& colours);

	// WARNING: Draws the card's shadow on the foreground around the card, never over it, then ends the tooltip.
	void EndTipCard(const DashboardColours& colours);

	// WARNING: Draws inside BeginTipCard/EndTooltip, ending with one Dummy the size of the card.
	void DrawTipCard(const TipCard& card, const DashboardColours& colours);
}
