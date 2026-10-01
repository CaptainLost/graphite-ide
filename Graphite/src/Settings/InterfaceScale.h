#pragma once

#include <array>
#include <string>

namespace Graphite {

	// The interface scales the settings offer, in percent of the size the system asks for.
	// A list rather than a free number: each step is one a user can tell from the next, and
	// a scale picked by dragging would rebuild the whole interface under the pointer on every
	// frame of the drag.
	inline constexpr std::array<int, 8> kInterfaceScalePercents{75, 90, 100, 110, 125, 150, 175, 200};

	inline constexpr int kDefaultInterfaceScalePercent = 100;

	// Reads a stored scale: 100 when it is absent or not a positive number, otherwise the
	// offered scale nearest to it, the smaller of two equally near.
	int InterfaceScalePercentOrDefault(const std::string& stored);
}
