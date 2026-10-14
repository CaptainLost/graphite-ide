#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	std::string RelativiseUnder(std::string_view message, const std::filesystem::path& root);
}
