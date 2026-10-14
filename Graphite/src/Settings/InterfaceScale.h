#pragma once

#include <array>
#include <string>

namespace Graphite {

	inline constexpr std::array<int, 8> kInterfaceScalePercents{75, 90, 100, 110, 125, 150, 175, 200};

	inline constexpr int kDefaultInterfaceScalePercent = 100;

	int InterfaceScalePercentOrDefault(const std::string& stored);
}
