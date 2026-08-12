#pragma once

#include "Core/Base/Result.h"
#include "Core/Text/Encoding.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
}