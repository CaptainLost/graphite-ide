#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace Core {

	std::vector<std::size_t> WrapColumns(std::string_view line,
										 std::size_t columnsPerRow,
										 std::size_t tabWidth);

	class TextBuffer;
	class RowMap;

	struct VisualPosition
	{
		std::size_t row = 0;
		std::size_t column = 0;
	};

	VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows,
								 std::size_t offset, std::size_t tabWidth);

	std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows,
							  VisualPosition position, std::size_t tabWidth);
}