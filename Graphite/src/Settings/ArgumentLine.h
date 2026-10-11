#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// The rules a Windows command line is split by, so a line JoinArgumentLine wrote comes back as the arguments it
	// joined, and an argument reaches the server as ChildProcess quotes it.
	std::vector<std::string> SplitArgumentLine(std::string_view line);

	std::string JoinArgumentLine(const std::vector<std::string>& arguments);
}
