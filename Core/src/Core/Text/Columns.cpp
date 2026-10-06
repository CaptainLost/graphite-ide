#include "corepch.h"

#include "Core/Text/Columns.h"

#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Core {

	std::size_t ColumnAfterTab(std::size_t column, std::size_t tabWidth)
	{
		const std::size_t nonZeroTabWidth = tabWidth == 0 ? 1 : tabWidth;

		return column + nonZeroTabWidth - column % nonZeroTabWidth;
	}

	std::size_t ColumnOfByte(std::string_view lineText, std::size_t byteOffset, std::size_t tabWidth)
	{
		const std::size_t stopOffset = std::min(byteOffset, lineText.size());

		std::size_t column = 0;
		std::size_t characterStart = 0;

		while (characterStart < stopOffset)
		{
			if (lineText[characterStart] == '\t')
			{
				column = ColumnAfterTab(column, tabWidth);
				++characterStart;
				continue;
			}

			++column;
			characterStart = NextCharacter(lineText, characterStart);
		}

		return column;
	}

	std::size_t ByteOfColumn(std::string_view lineText, std::size_t column, std::size_t tabWidth)
	{
		std::size_t characterStart = 0;
		std::size_t characterColumn = 0;

		while (characterStart < lineText.size())
		{
			const bool isTab = lineText[characterStart] == '\t';
			const std::size_t nextCharacterColumn =
				isTab ? ColumnAfterTab(characterColumn, tabWidth) : characterColumn + 1;

			const bool columnFallsInsideThisCharacter = column < nextCharacterColumn;
			if (columnFallsInsideThisCharacter)
			{
				return characterStart;
			}

			characterColumn = nextCharacterColumn;
			characterStart = isTab ? characterStart + 1 : NextCharacter(lineText, characterStart);
		}

		return lineText.size();
	}

	std::size_t ColumnCount(std::string_view lineText, std::size_t tabWidth)
	{
		return ColumnOfByte(lineText, lineText.size(), tabWidth);
	}

	std::size_t ExpandedOffsetOfByte(std::string_view lineText, std::size_t byteOffset, std::size_t tabWidth)
	{
		const std::size_t stopOffset = std::min(byteOffset, lineText.size());

		std::size_t expandedOffset = 0;
		std::size_t column = 0;
		std::size_t characterStart = 0;

		while (characterStart < stopOffset)
		{
			if (lineText[characterStart] == '\t')
			{
				const std::size_t nextTabStop = ColumnAfterTab(column, tabWidth);

				expandedOffset += nextTabStop - column;
				column = nextTabStop;
				++characterStart;
				continue;
			}

			const std::size_t nextCharacterStart = NextCharacter(lineText, characterStart);

			expandedOffset += nextCharacterStart - characterStart;
			++column;
			characterStart = nextCharacterStart;
		}

		return expandedOffset;
	}

	std::string ExpandTabs(std::string_view lineText, std::size_t tabWidth)
	{
		std::string expanded;
		expanded.reserve(lineText.size());

		std::size_t column = 0;
		std::size_t characterStart = 0;

		while (characterStart < lineText.size())
		{
			if (lineText[characterStart] == '\t')
			{
				const std::size_t nextTabStop = ColumnAfterTab(column, tabWidth);

				expanded.append(nextTabStop - column, ' ');
				column = nextTabStop;
				++characterStart;
				continue;
			}

			const std::size_t nextCharacterStart = NextCharacter(lineText, characterStart);

			expanded.append(lineText.substr(characterStart, nextCharacterStart - characterStart));
			++column;
			characterStart = nextCharacterStart;
		}

		return expanded;
	}

	std::size_t WidestColumn(const TextBuffer& buffer, std::size_t tabWidth)
	{
		std::size_t widest = 0;

		for (std::size_t lineIndex = 0; lineIndex < buffer.GetLineCount(); ++lineIndex)
		{
			widest = std::max(widest, ColumnCount(buffer.LineText(lineIndex), tabWidth));
		}

		return widest;
	}
}
