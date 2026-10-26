#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	class CancellationToken;
	class TextBuffer;

	std::size_t IndentDepth(std::string_view lineText, std::size_t tabWidth);

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer,
										  std::size_t tabWidth);

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer,
										  std::size_t tabWidth,
										  std::size_t firstLine,
										  std::size_t lastLine);

	std::optional<std::vector<std::size_t>> IndentDepthsUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
																		const CancellationToken& cancellation);

	std::optional<std::vector<std::size_t>> IndentDepthsUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
																		std::size_t firstLine, std::size_t lastLine,
																		const CancellationToken& cancellation);
}