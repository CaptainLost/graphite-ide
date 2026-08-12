#pragma once

#include <string>
#include <string_view>

namespace Core {

	std::string Lowered(std::string_view text);
	std::string Trimmed(std::string_view text);
}