#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	class CancellationToken;

	std::vector<std::size_t> WrapColumns(std::string_view lineText, std::size_t columnsPerRow, std::size_t tabWidth);

	std::optional<std::vector<std::size_t>> WrapColumnsUnlessCancelled(std::string_view lineText, std::size_t columnsPerRow,
														   std::size_t tabWidth, const CancellationToken& cancellation);

	class TextBuffer;
	class RowMap;

	struct VisualPosition
	{
		std::size_t Row = 0;
		std::size_t Column = 0;
	};

	VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows, std::size_t offset, std::size_t tabWidth);

	std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows, VisualPosition position, std::size_t tabWidth);
}
