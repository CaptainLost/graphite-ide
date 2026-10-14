#include "UI/SideBySide.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	SideBySideWidths SplitSideBySide(float room, float wanted, float minimum)
	{
		const bool roomHasWidth = room > 0.0f;

		if (!roomHasWidth)
		{
			return {};
		}

		constexpr float kHalf = 0.5f;

		const float smallestSide = std::clamp(minimum, 0.0f, room * kHalf);
		const float wantedOrSmallest = std::isnan(wanted) ? smallestSide : wanted;
		const float left = std::clamp(wantedOrSmallest, smallestSide, room - smallestSide);

		return {left, room - left};
	}
}
