#pragma once

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Core {

	enum class HashAlgorithm : std::uint8_t
	{
		Sha256,
		Sha512
	};

	Result<std::vector<std::uint8_t>> HashBytes(HashAlgorithm algorithm, std::string_view bytes);

	// Reads the file in pieces, so a download of any size hashes in constant memory.
	Result<std::vector<std::uint8_t>> HashFile(HashAlgorithm algorithm, const std::filesystem::path& file);
}
