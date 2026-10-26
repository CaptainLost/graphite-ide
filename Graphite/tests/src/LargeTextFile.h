#pragma once

#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

inline constexpr std::uint64_t kLargeTextFileSize = Core::LargeFileThreshold + 1024ull * 1024;

// A text file a megabyte past the size that opens without asking. Its first sample is text, so the probe takes it for
// text; the rest is what the file system fills a grown file with, never written by the test.
inline std::filesystem::path MakeLargeTextFile(const std::filesystem::path& path)
{
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream << std::string(Core::ProbeSampleSize, 'x');
	}

	std::error_code resizeError;
	std::filesystem::resize_file(path, kLargeTextFileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	return path;
}
