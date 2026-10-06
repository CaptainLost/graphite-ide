#pragma once

#include <string_view>

namespace Core {

	bool GlobMatches(std::string_view pattern, std::string_view text);
}
