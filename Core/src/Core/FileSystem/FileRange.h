#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace Core {

	// The file is opened for this read and closed before returning, sharing read, write and delete with everyone else,
	// so a view of a large file never keeps it from being written, renamed or removed. The result is shorter than count
	// at the end of the file, and empty at or past it.
	Result<std::vector<std::uint8_t>> ReadFileRange(const std::filesystem::path& path, std::uint64_t offset, std::size_t count);
}
