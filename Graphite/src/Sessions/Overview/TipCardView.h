#pragma once

#include "Sessions/Overview/DashboardColours.h"
#include "Sessions/Overview/DashboardTips.h"

namespace Graphite {

	// WARNING: Draws inside BeginTooltip/EndTooltip, ending with one Dummy the size of the card.
	void DrawTipCard(const TipCard& card, const DashboardColours& colours);
}
