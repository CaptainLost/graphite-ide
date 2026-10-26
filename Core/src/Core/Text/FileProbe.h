#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

	enum class FileKind
	{
		Text,
		LargeText,
		Binary,
		TooLarge
	};

	inline constexpr std::uint64_t LargeFileThreshold = 16ull * 1024 * 1024;

	// A file this size or smaller reads within a frame, so it opens where it is asked for; a larger one is read on a
	// worker while its tab waits.
	inline constexpr std::uint64_t BackgroundLoadThreshold = 4ull * 1024 * 1024;

	inline bool LoadsInBackground(std::uint64_t fileSize)
	{
		return fileSize > BackgroundLoadThreshold;
	}

	inline constexpr std::uint64_t MaximumFileSize = 512ull * 1024 * 1024;

	inline constexpr std::size_t ProbeSampleSize = 8192;

	struct FileProbe
	{
		FileKind Kind = FileKind::Text;
		Encoding FileEncoding = Encoding::Utf8;
		std::uint64_t Size = 0;
	};

	FileProbe ClassifySample(std::string_view sample, std::uint64_t fileSize);

	Result<FileProbe> ProbeFile(const std::filesystem::path& path);

	enum class DiskTextOutcome
	{
		Read,
		Binary,
		TooLarge,
		Unreadable,
		Undecodable
	};

	struct DiskText
	{
		DiskTextOutcome Outcome = DiskTextOutcome::Unreadable;
		std::string Text;
	};

	DiskText ReadDiskText(const std::filesystem::path& path);
}