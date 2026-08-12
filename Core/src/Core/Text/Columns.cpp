#include "corepch.h"

#include "Core/Text/Columns.h"

#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Core {

	std::size_t ColumnAfterTab(std::size_t column, std::size_t tabWidth)
	{
		const std::size_t width = tabWidth == 0 ? 1 : tabWidth;

		return column + width - column % width;
	}

	std::size_t ColumnOfByte(std::string_view line, std::size_t byte, std::size_t tabWidth)
	{
		const std::size_t target = std::min(byte, line.size());

		std::size_t column = 0;
		std::size_t at = 0;

		while (at < target)
		{
			if (line[at] == '\t')
			{
				column = ColumnAfterTab(column, tabWidth);
				++at;
				continue;
			}

			++column;
			at = NextCharacter(line, at);
		}

		return column;
	}

	std::size_t ByteOfColumn(std::string_view line, std::size_t column, std::size_t tabWidth)
	{
		std::size_t at = 0;
		std::size_t current = 0;

		while (at < line.size())
		{
			// A tab covers a run of columns and a character has no inside, so any column
			// before the next boundary resolves to the byte that starts this character.
			const std::size_t next =
				line[at] == '\t' ? ColumnAfterTab(current, tabWidth) : current + 1;

			if (column < next)
				return at;

			current = next;
			at = line[at] == '\t' ? at + 1 : NextCharacter(line, at);
		}

		return line.size();
	}

	std::size_t ColumnCount(std::string_view line, std::size_t tabWidth)
	{
		return ColumnOfByte(line, line.size(), tabWidth);
	}

	std::string ExpandTabs(std::string_view line, std::size_t tabWidth)
	{
		std::string expanded;
		expanded.reserve(line.size());

		std::size_t column = 0;
		std::size_t at = 0;

		while (at < line.size())
		{
			if (line[at] == '\t')
			{
				const std::size_t next = ColumnAfterTab(column, tabWidth);

				expanded.append(next - column, ' ');
				column = next;
				++at;
				continue;
			}

			// One column per character, however many bytes it takes — the same rule
			// ColumnOfByte applies, so the two cannot disagree about where a byte lands.
			const std::size_t next = NextCharacter(line, at);

			expanded.append(line.substr(at, next - at));
			++column;
			at = next;
		}

		return expanded;
	}

	std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth)
	{
		std::size_t widest = 0;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
			widest = std::max(widest, ColumnCount(buffer.LineText(line), tabWidth));

		return widest;
	}
}