#include "UI/SideBySide.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	SideBySideWidths SplitSideBySide(float room, float wanted, float minimum)
	{
		// Also refuses a NaN room, which fails every comparison.
		if (!(room > 0.0f))
			return {};

		const float least = std::clamp(minimum, 0.0f, room * 0.5f);

		// Clamped between two limits that never cross: the lower is at most half the room and
		// the upper at least half. A NaN wanted is taken as the least the left may have.
		const float left = std::isnan(wanted) ? least : std::clamp(wanted, least, room - least);

		return {left, room - left};
	}
}
