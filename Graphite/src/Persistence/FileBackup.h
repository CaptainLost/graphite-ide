#pragma once

#include <filesystem>
#include <string_view>

namespace Graphite {

	bool BackUpBeforeWritingOver(const std::filesystem::path& file, std::string_view reason);
}
