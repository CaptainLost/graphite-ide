#pragma once

#include <chrono>
#include <string>

namespace Graphite {

	// At most four characters: "now", "59m", "23h", "13d", "8w", "12mo", "3y".
	std::string FormatBranchAge(std::chrono::sys_seconds tip, std::chrono::sys_seconds now);
}