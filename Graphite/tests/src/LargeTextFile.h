#pragma once

#include <doctest/doctest.h>

#include "Core/Text/FileProbe.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

inline constexpr std::uint64_t kLargeTextFileSize = Core::LargeFileThreshold + 1024ull * 1024;

// WARNING: The probe reads only the first sample, so the rest of the grown file is never written.
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
