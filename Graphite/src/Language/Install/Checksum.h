#pragma once

#include "Core/Base/Result.h"
#include "Core/Platform/Hash.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct Checksum
	{
		Core::HashAlgorithm Algorithm = Core::HashAlgorithm::Sha256;
		std::vector<std::uint8_t> Digest;

		bool operator==(const Checksum&) const = default;
	};

	std::string DescribeChecksum(const Checksum& checksum);

	std::optional<Checksum> ParseChecksum(std::string_view text);

	Core::Result<void> VerifyChecksum(const std::filesystem::path& file, const Checksum& expected);
}
