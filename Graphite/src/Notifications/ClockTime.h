#pragma once

#include <chrono>
#include <string>

namespace Graphite {

	std::string FormatClockTime(std::chrono::system_clock::time_point moment, const std::chrono::time_zone* zone);
}
