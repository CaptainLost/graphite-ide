#pragma once

namespace Graphite {

	struct SideBySideWidths
	{
		float Left = 0.0f;
		float Right = 0.0f;
	};

	SideBySideWidths SplitSideBySide(float room, float wanted, float minimum);
}
