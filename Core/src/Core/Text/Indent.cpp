#include "corepch.h"

#include "Core/Text/Indent.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <optional>

namespace Core {

	namespace {
		bool IsIndentWhitespace(char c)
		{
			return c == ' ' || c == '\t';
		}

		bool IsBlank(std::string_view line)
		{
			for (const char c : line)
			{
				if (!IsIndentWhitespace(c))
					return false;
			}

			return true;
		}
	}

	std::size_t IndentDepth(std::string_view line, std::size_t tabWidth)
	{
		std::size_t byte = 0;

		while (byte < line.size() && IsIndentWhitespace(line[byte]))
			++byte;

		if (byte == line.size())
			return 0;

		return ColumnOfByte(line, byte, tabWidth);
	}

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer, std::size_t tabWidth)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		return IndentDepths(buffer, tabWidth, 0, lineCount - 1);
	}

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer, std::size_t tabWidth,
										  std::size_t firstLine, std::size_t lastLine)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		firstLine = std::min(firstLine, lineCount - 1);
		lastLine = std::min(lastLine, lineCount - 1);

		const std::size_t rangeSize = lastLine - firstLine + 1;
		std::vector<std::size_t> depths(rangeSize, 0);
		std::vector<bool> blank(rangeSize, false);

		for (std::size_t line = firstLine; line <= lastLine; ++line)
		{
			const std::string text = buffer.LineText(line);
			const std::size_t index = line - firstLine;
			blank[index] = IsBlank(text);

			if (!blank[index])
				depths[index] = IndentDepth(text, tabWidth);
		}

		// Nearest non-blank depth before firstLine, found outside the range when the
		// range itself starts with a run of blank lines.
		std::optional<std::size_t> before;

		for (std::size_t line = firstLine; !before && line-- > 0;)
		{
			const std::string text = buffer.LineText(line);

			if (!IsBlank(text))
				before = IndentDepth(text, tabWidth);
		}

		// Nearest non-blank depth after lastLine, symmetrically.
		std::optional<std::size_t> after;

		for (std::size_t line = lastLine + 1; !after && line < lineCount; ++line)
		{
			const std::string text = buffer.LineText(line);

			if (!IsBlank(text))
				after = IndentDepth(text, tabWidth);
		}

		// Nearest non-blank depth at or before each line in the range, carried forward
		// from `before`.
		std::vector<std::optional<std::size_t>> above(rangeSize);
		std::optional<std::size_t> lastNonBlank = before;

		for (std::size_t index = 0; index < rangeSize; ++index)
		{
			if (!blank[index])
				lastNonBlank = depths[index];

			above[index] = lastNonBlank;
		}

		// Nearest non-blank depth at or after each line in the range, carried backward
		// from `after`.
		std::vector<std::optional<std::size_t>> below(rangeSize);
		std::optional<std::size_t> nextNonBlank = after;

		for (std::size_t index = rangeSize; index-- > 0;)
		{
			if (!blank[index])
				nextNonBlank = depths[index];

			below[index] = nextNonBlank;
		}

		for (std::size_t index = 0; index < rangeSize; ++index)
		{
			if (!blank[index])
				continue;

			// A blank line belongs to neither the block above nor the block below, so a
			// guide is only honest at the shallower of the two — the smaller of the two
			// stops a guide from being drawn into a gap where the deeper block has
			// already closed. A missing side counts as depth 0 here, which is exactly
			// what "the depth is 0 with nothing on one side" means once folded into the
			// same minimum rather than handled as a separate case.
			depths[index] = std::min(above[index].value_or(0), below[index].value_or(0));
		}

		return depths;
	}
}