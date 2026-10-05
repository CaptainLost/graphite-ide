#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

inline void WriteFileBytes(const std::filesystem::path& path, std::string_view contents)
{
	std::ofstream stream{path, std::ios::binary};
	stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// Every byte, or "" when the file cannot be opened.
inline std::string ReadWholeFile(const std::filesystem::path& path)
{
	std::ifstream stream{path, std::ios::binary};
	return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}
