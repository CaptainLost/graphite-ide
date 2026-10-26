#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <functional>

namespace Core {

	using CopiedFileCount = std::function<void(std::size_t copiedFiles)>;

	Result<void> CreateEmptyFile(const std::filesystem::path& path);

	Result<void> MakeDirectory(const std::filesystem::path& path);

	Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to);

	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path,
											const CopiedFileCount& onFileCopied = CopiedFileCount{});

	Result<std::filesystem::path> DuplicateTo(const std::filesystem::path& path, const std::filesystem::path& destination,
											  const CopiedFileCount& onFileCopied = CopiedFileCount{});

	Result<void> MoveToRecycleBin(const std::filesystem::path& path);

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path);
}
