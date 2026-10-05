#pragma once

#include <imgui.h>

namespace Graphite {

	// Draws the application's mark — the faces of Graphite.svg without the disc behind them,
	// so it sits on whatever surface is under it — scaled to `height` pixels tall and centred
	// on `centre`. Vector rather than a texture, so it stays sharp at every interface scale.
	// The SVG comes from LogoSvg.h, which scripts/Update-LogoSvg.ps1 writes.
	void DrawLogo(ImDrawList* draw, ImVec2 centre, float height);

	// The mark's width for a given height.
	float LogoWidth(float height);
}
