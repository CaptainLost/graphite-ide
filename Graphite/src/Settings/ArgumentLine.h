#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	std::vector<std::string> SplitArgumentLine(std::string_view line);

	std::string JoinArgumentLine(const std::vector<std::string>& arguments);
}
