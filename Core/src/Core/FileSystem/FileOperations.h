#pragma once

#include "Core/Base/Result.h"

#include <cstddef>
#include <filesystem>
#include <functional>

namespace Core {

	// Told how many files a copy has copied so far, after each one, on the thread that copies.
	using CopiedFileCount = std::function<void(std::size_t copiedFiles)>;

	Result<void> CreateEmptyFile(const std::filesystem::path& path);

	Result<void> MakeDirectory(const std::filesystem::path& path);

	Result<void> Rename(const std::filesystem::path& from, const std::filesystem::path& to);

	// A folder is copied whole, links inside it copied as links. Answers the copy's path, beside the original.
	Result<std::filesystem::path> Duplicate(const std::filesystem::path& path,
											const CopiedFileCount& onFileCopied = CopiedFileCount{});

	Result<void> MoveToRecycleBin(const std::filesystem::path& path);

	std::filesystem::path MakeUniquePath(const std::filesystem::path& path);
}