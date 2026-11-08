#pragma once

#include "Core/Base/PlatformDetection.h"
#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	struct DirectoryEntry
	{
		std::filesystem::path Path;
		std::string Name;
		bool IsDirectory = false;
		bool IsHidden = false;
		std::uint64_t Size = 0;
	};

	Result<std::vector<DirectoryEntry>> ReadDirectory(const std::filesystem::path& path);

	Result<std::string> ReadFileBytes(const std::filesystem::path& path,
									  std::optional<std::uintmax_t> maximumBytes = std::nullopt);

	bool PathExists(const std::filesystem::path& path);

	std::string ToUtf8(const std::filesystem::path& path);

	std::string ToGenericUtf8(const std::filesystem::path& path);

	std::filesystem::path FromUtf8(std::string_view text);

#ifdef CORE_PLATFORM_WINDOWS
	std::wstring ToUtf16(std::string_view text);
#endif

	constexpr bool IsPathSeparator(char character)
	{
		return character == '/' || character == '\\';
	}

	// The result is a view into `text`, so `text` has to outlive it.
	constexpr std::string_view AfterLastPathSeparator(std::string_view text)
	{
		const std::size_t lastSeparator = text.find_last_of("\\/");

		if (lastSeparator == std::string_view::npos)
		{
			return text;
		}

		return text.substr(lastSeparator + 1);
	}

	std::string LoweredExtension(const std::filesystem::path& path);

	int CompareNames(std::string_view left, std::string_view right);

	// As CompareNames, but a run of digits compares by its value: file2 before file10, file231 after file230. Two names
	// that differ only in leading zeros are not equal; the one with fewer comes first.
	int CompareNamesNaturally(std::string_view left, std::string_view right);

	// Lexical, so it serves a path whose file may no longer exist, which std::filesystem::equivalent
	// cannot: equivalent asks the file system and fails for a missing file.
	std::filesystem::path NormalizePath(const std::filesystem::path& path);

	using PathKeyText = std::filesystem::path::string_type;

	// Windows folds case beyond ASCII in a file name, so paths are compared through this key, never
	// through their spelling.
	PathKeyText PathKey(const std::filesystem::path& path);

	// A suffix match is not a substitute: a file at the repository root has an empty parent path, so
	// "ends with" degenerates into "the same name anywhere in the tree".
	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right);

	std::filesystem::path NormalizeDirectory(const std::filesystem::path& path);

	std::optional<std::filesystem::path> RelativeTo(
		const std::filesystem::path& path, const std::filesystem::path& base);

	std::optional<std::filesystem::path> RelativeToKey(
		const std::filesystem::path& path, const PathKeyText& baseKey);

	bool IsSameOrWithin(const std::filesystem::path& path, const std::filesystem::path& root);
}
