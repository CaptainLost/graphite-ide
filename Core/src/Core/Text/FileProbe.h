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

	inline constexpr std::uint64_t MaximumFileSize = 512ull * 1024 * 1024;

	inline constexpr std::size_t ProbeSampleSize = 8192;

	struct FileProbe
	{
		FileKind Kind = FileKind::Text;
		Encoding FileEncoding = Encoding::Utf8;
		std::uint64_t Size = 0;
	};

	FileProbe ClassifySample(std::string_view sample, std::uint64_t size);

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

	// LargeText is read. The byte order mark is kept: a UTF-8 mark stays as EF BB BF, a UTF-16 mark
	// decodes to a leading U+FEFF.
	DiskText ReadDiskText(const std::filesystem::path& path);
}