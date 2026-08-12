#pragma once

#include "Core/Base/Result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct DirectoryEntry
	{
		std::filesystem::path path;
		std::string name;
		bool isDirectory = false;
		bool isHidden = false;
		std::uint64_t size = 0;
	};

	Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path);

	std::string ToUtf8(const std::filesystem::path& path);

	std::filesystem::path FromUtf8(std::string_view text);

	int CompareNames(std::string_view left, std::string_view right);

	std::filesystem::path NormalizeDirectory(const std::filesystem::path& path);

	std::optional<std::filesystem::path> RelativeTo(
		const std::filesystem::path& path, const std::filesystem::path& base);
}