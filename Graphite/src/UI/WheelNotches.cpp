#include "UI/WheelNotches.h"

#include <cmath>

namespace Graphite {

	int TakeWholeNotches(float& pending, float wheel)
	{
		pending += wheel;
		const int notches = static_cast<int>(std::trunc(pending));
		pending -= static_cast<float>(notches);
		return notches;
	}
}
