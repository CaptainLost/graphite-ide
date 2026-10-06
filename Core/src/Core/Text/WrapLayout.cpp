#include "corepch.h"

#include "Core/Text/WrapLayout.h"

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace Core {

	namespace {
		constexpr std::size_t kNarrowestRowThatStillAdvances = 1;
		constexpr std::size_t kLineStartColumn = 0;

		struct RowScan
		{
			std::size_t EndByte = 0;
			std::size_t EndColumn = 0;

			bool FoundSpace = false;
			std::size_t LastSpaceByte = 0;
			std::size_t LastSpaceColumn = 0;

			bool FoundLastCellCharacter = false;
			bool LastCellCharacterIsTab = false;
			std::size_t LastCellCharacterByte = 0;
			std::size_t LastCellCharacterColumn = 0;
			std::size_t ByteAfterLastCellCharacter = 0;
			std::size_t ColumnAfterLastCellCharacter = 0;
		};

		struct WrappedRow
		{
			std::size_t SubRow = 0;
			std::size_t StartColumn = 0;
		};

		// Byte and column advance together on purpose: deriving one from the other for each character
		// re-walks the line from its start and makes wrapping one line quadratic in its length.
		RowScan ScanToLimit(std::string_view lineText, std::size_t tabWidth, std::size_t limitColumn, std::size_t byteOffset, std::size_t column)
		{
			const std::size_t lastCellColumn = limitColumn - 1;
			RowScan scan;

			while (byteOffset < lineText.size() && column < limitColumn)
			{
				const bool isTab = lineText[byteOffset] == '\t';
				const std::size_t nextByte = isTab ? byteOffset + 1 : NextCharacter(lineText, byteOffset);
				const std::size_t nextColumn = isTab ? ColumnAfterTab(column, tabWidth) : column + 1;

				const bool isSpace = lineText[byteOffset] == ' ';
				if (isSpace)
				{
					scan.LastSpaceByte = byteOffset;
					scan.LastSpaceColumn = column;
					scan.FoundSpace = true;
				}

				const bool coversLastCell = column <= lastCellColumn && lastCellColumn < nextColumn;
				if (coversLastCell)
				{
					scan.LastCellCharacterByte = byteOffset;
					scan.LastCellCharacterColumn = column;
					scan.ByteAfterLastCellCharacter = nextByte;
					scan.ColumnAfterLastCellCharacter = nextColumn;
					scan.LastCellCharacterIsTab = isTab;
					scan.FoundLastCellCharacter = true;
				}

				byteOffset = nextByte;
				column = nextColumn;
			}

			scan.EndByte = byteOffset;
			scan.EndColumn = column;

			return scan;
		}

		void SkipWholeSpaceRun(std::string_view lineText, std::size_t lineColumnCount, std::size_t& byteOffset, std::size_t& column)
		{
			while (byteOffset < lineText.size() && column < lineColumnCount)
			{
				const bool isSpace = lineText[byteOffset] == ' ';
				if (!isSpace)
				{
					break;
				}

				++byteOffset;
				++column;
			}
		}

		WrappedRow WrappedRowOfColumn(const std::vector<std::size_t>& breaks, std::size_t column)
		{
			WrappedRow wrappedRow;

			for (std::size_t breakIndex = 0; breakIndex < breaks.size(); ++breakIndex)
			{
				const std::size_t breakColumn = breaks[breakIndex];
				const bool columnIsAtOrPastBreak = column >= breakColumn;

				if (!columnIsAtOrPastBreak)
				{
					break;
				}

				const std::size_t subRowStartedByBreak = breakIndex + 1;
				wrappedRow.SubRow = subRowStartedByBreak;
				wrappedRow.StartColumn = breakColumn;
			}

			return wrappedRow;
		}
	}

	std::vector<std::size_t> WrapColumns(std::string_view lineText, std::size_t columnsPerRow, std::size_t tabWidth)
	{
		std::vector<std::size_t> breaks;

		const std::size_t rowWidth = std::max(columnsPerRow, kNarrowestRowThatStillAdvances);
		const std::size_t lineColumnCount = ColumnCount(lineText, tabWidth);

		std::size_t byteOffset = 0;
		std::size_t column = 0;
		std::size_t rowStartColumn = 0;

		while (lineColumnCount - rowStartColumn > rowWidth)
		{
			const std::size_t limitColumn = rowStartColumn + rowWidth;
			const RowScan scan = ScanToLimit(lineText, tabWidth, limitColumn, byteOffset, column);

			byteOffset = scan.EndByte;
			column = scan.EndColumn;

			// The loop condition means the scan always reaches the row's last cell. Stopping when it
			// somehow does not, rather than guessing a break, keeps this loop from standing still.
			if (!scan.FoundLastCellCharacter)
			{
				break;
			}

			const bool tabCoveringLastCellCanMoveToNextRow = scan.LastCellCharacterIsTab && scan.LastCellCharacterColumn != rowStartColumn;
			std::size_t breakColumn = 0;

			if (scan.FoundSpace)
			{
				byteOffset = scan.LastSpaceByte + 1;
				column = scan.LastSpaceColumn + 1;

				SkipWholeSpaceRun(lineText, lineColumnCount, byteOffset, column);

				breakColumn = column;
			}
			else if (tabCoveringLastCellCanMoveToNextRow)
			{
				byteOffset = scan.LastCellCharacterByte;
				column = scan.LastCellCharacterColumn;
				breakColumn = scan.LastCellCharacterColumn;
			}
			else
			{
				byteOffset = scan.ByteAfterLastCellCharacter;
				column = scan.ColumnAfterLastCellCharacter;
				breakColumn = scan.ColumnAfterLastCellCharacter;
			}

			const bool breakWouldStartEmptyRow = breakColumn >= lineColumnCount;
			if (breakWouldStartEmptyRow)
			{
				break;
			}

			breaks.push_back(breakColumn);
			rowStartColumn = breakColumn;
		}

		return breaks;
	}

	VisualPosition CaretToVisual(const TextBuffer& buffer, const RowMap& rows, std::size_t offset, std::size_t tabWidth)
	{
		const Position caretPosition = buffer.ToPosition(offset);
		const std::size_t lineIndex = caretPosition.Line;
		const std::size_t caretByteInLine = caretPosition.Column;
		const std::string lineText = buffer.LineText(lineIndex);
		const std::size_t caretColumn = ColumnOfByte(lineText, caretByteInLine, tabWidth);

		const WrappedRow wrappedRow = WrappedRowOfColumn(rows.BreaksOf(lineIndex), caretColumn);

		return VisualPosition{.Row = rows.FirstRowOf(lineIndex) + wrappedRow.SubRow, .Column = caretColumn - wrappedRow.StartColumn};
	}

	std::size_t VisualToCaret(const TextBuffer& buffer, const RowMap& rows, VisualPosition position, std::size_t tabWidth)
	{
		const RowRef visualRow = rows.RowAt(position.Row);
		const std::string lineText = buffer.LineText(visualRow.Line);

		const std::vector<std::size_t>& breaks = rows.BreaksOf(visualRow.Line);

		// After RowMap::SetRowsOf a sub-row may have no break to start at; starting it at the line's start
		// gives a wrong caret, not a read past the breaks.
		const bool rowStartsAtLineStart = visualRow.SubRow == 0 || visualRow.SubRow > breaks.size();
		const std::size_t rowStartColumn = rowStartsAtLineStart ? kLineStartColumn : breaks[visualRow.SubRow - 1];

		const bool anotherSubRowFollows = visualRow.SubRow < breaks.size();
		const std::size_t lastCaretColumnOnRow = anotherSubRowFollows ? breaks[visualRow.SubRow] - 1 : ColumnCount(lineText, tabWidth);

		const std::size_t caretColumn = std::min(rowStartColumn + position.Column, lastCaretColumnOnRow);

		return buffer.LineStart(visualRow.Line) + ByteOfColumn(lineText, caretColumn, tabWidth);
	}
}
