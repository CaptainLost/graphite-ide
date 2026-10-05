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

	// The whole file as bytes, untouched: no decoding, no newline translation. NotFound when
	// there is no such file, AccessDenied when it exists but cannot be opened, IoFailure for
	// anything else, and TooLarge when it holds more than `maximumBytes` - checked against the
	// size before reading and again while reading, so a file that grows mid-read is still cut
	// off. No limit when `maximumBytes` is empty.
	Result<std::string> ReadFileBytes(const std::filesystem::path& path,
									  std::optional<std::uintmax_t> maximumBytes = std::nullopt);

	bool PathExists(const std::filesystem::path& path);

	std::string ToUtf8(const std::filesystem::path& path);

	// ToUtf8 with every separator written as a forward slash, the way git spells paths.
	std::string ToGenericUtf8(const std::filesystem::path& path);

	std::filesystem::path FromUtf8(std::string_view text);

#ifdef CORE_PLATFORM_WINDOWS
	std::wstring ToUtf16(std::string_view text);
#endif

	constexpr bool IsPathSeparator(char character)
	{
		return character == '/' || character == '\\';
	}

	// A view into `text`.
	constexpr std::string_view AfterLastPathSeparator(std::string_view text)
	{
		const std::size_t lastSeparator = text.find_last_of("\\/");

		if (lastSeparator == std::string_view::npos)
		{
			return text;
		}

		return text.substr(lastSeparator + 1);
	}

	// The extension with its dot, ASCII-lowered: ".vcxproj" for "App.VCXPROJ", empty for none.
	std::string LoweredExtension(const std::filesystem::path& path);

	int CompareNames(std::string_view left, std::string_view right);

	// lexically_normal collapses "a/../b" into "b" without touching the filesystem - safe for
	// a path naming a file that may no longer exist, unlike equivalent(). make_preferred
	// converts forward and native separators to the same spelling, so two paths that name the
	// same file byte-for-byte compare equal regardless of which convention either came from.
	// Unlike NormalizeDirectory, a trailing separator's empty final element is left alone: a
	// file path never carries one to begin with.
	std::filesystem::path NormalizePath(const std::filesystem::path& path);

	using PathKeyText = std::filesystem::path::string_type;

	// The one form every path in this codebase is compared in: normalized as NormalizeDirectory
	// does, then upper-cased the way Windows folds a file name - every letter it treats as the
	// same, not only ASCII. Two spellings of one place have the same key, so it serves as a map
	// key as well as for equality. Lexical: the path need not exist. Empty for an empty path.
	PathKeyText PathKey(const std::filesystem::path& path);

	// Whole-path equality through PathKey. A filename or suffix match is not safe here: a
	// repository-root file has an empty parent_path, so matching on "ends with" degenerates
	// into "same name anywhere in the tree".
	bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right);

	std::filesystem::path NormalizeDirectory(const std::filesystem::path& path);

	// `path` spelled relative to `base`, or nothing when it does not lie at or below it; "." for
	// the base itself. Compared through PathKey, so the editor and git spelling one directory two
	// ways - a drive letter, for one - still agree. Nothing for an empty base.
	std::optional<std::filesystem::path> RelativeTo(
		const std::filesystem::path& path, const std::filesystem::path& base);

	// RelativeTo for a caller that asks about many paths under one base: `baseKey` is
	// PathKey(base), made once. An empty key gives nothing.
	std::optional<std::filesystem::path> RelativeToKey(
		const std::filesystem::path& path, const PathKeyText& baseKey);

	// Whether `path` is `root` or lies below it, compared through PathKey, so case and slash
	// direction do not matter and "D:\Repos2" is not inside "D:\Repos". Lexical - neither path
	// has to exist. False for an empty root and for a relative path on either side, neither of
	// which names a place.
	bool IsSameOrWithin(const std::filesystem::path& path, const std::filesystem::path& root);
}