#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace Core {

	class TextBuffer;

	std::size_t IndentDepth(std::string_view lineText, std::size_t tabWidth);

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer,
										  std::size_t tabWidth);

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer,
										  std::size_t tabWidth,
										  std::size_t firstLine,
										  std::size_t lastLine);
}