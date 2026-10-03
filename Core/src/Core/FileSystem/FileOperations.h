#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace Core {

	Result<void> CreateEmptyFile(const std::filesystem::path& path);

	Result<void> MakeDirectory(const std::filesystem::path& path);

	Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to);

	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path);

	Result<void> MoveToRecycleBin(const std::filesystem::path& path);

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path);

	bool IsValidFileName(std::string_view name);
}