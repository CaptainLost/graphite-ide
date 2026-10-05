#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

// Truncates. It never creates a missing parent directory: writing into one leaves no file.
inline void WriteFileBytes(const std::filesystem::path& path, std::string_view contents)
{
	std::ofstream stream{path, std::ios::binary};
	stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}
