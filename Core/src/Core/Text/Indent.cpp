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
				{
					return false;
				}
			}

			return true;
		}

		std::optional<std::size_t> DepthAboveRange(const TextBuffer& buffer, std::size_t firstLine,
												   std::size_t tabWidth)
		{
			for (std::size_t line = firstLine; line-- > 0;)
			{
				const std::string text = buffer.LineText(line);

				if (!IsBlank(text))
				{
					return IndentDepth(text, tabWidth);
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> DepthBelowRange(const TextBuffer& buffer, std::size_t lastLine,
												   std::size_t tabWidth)
		{
			for (std::size_t line = lastLine + 1; line < buffer.GetLineCount(); ++line)
			{
				const std::string text = buffer.LineText(line);

				if (!IsBlank(text))
				{
					return IndentDepth(text, tabWidth);
				}
			}

			return std::nullopt;
		}

		std::vector<std::optional<std::size_t>> CarryForward(
			const std::vector<std::size_t>& depths, const std::vector<bool>& blank,
			std::optional<std::size_t> seed)
		{
			std::vector<std::optional<std::size_t>> carried(depths.size());

			for (std::size_t index = 0; index < depths.size(); ++index)
			{
				if (!blank[index])
				{
					seed = depths[index];
				}

				carried[index] = seed;
			}

			return carried;
		}

		std::vector<std::optional<std::size_t>> CarryBackward(
			const std::vector<std::size_t>& depths, const std::vector<bool>& blank,
			std::optional<std::size_t> seed)
		{
			std::vector<std::optional<std::size_t>> carried(depths.size());

			for (std::size_t index = depths.size(); index-- > 0;)
			{
				if (!blank[index])
				{
					seed = depths[index];
				}

				carried[index] = seed;
			}

			return carried;
		}
	}

	std::size_t IndentDepth(std::string_view line, std::size_t tabWidth)
	{
		std::size_t byte = 0;

		while (byte < line.size() && IsIndentWhitespace(line[byte]))
		{
			++byte;
		}

		if (byte == line.size())
		{
			return 0;
		}

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
			{
				depths[index] = IndentDepth(text, tabWidth);
			}
		}

		const std::vector<std::optional<std::size_t>> above =
			CarryForward(depths, blank, DepthAboveRange(buffer, firstLine, tabWidth));

		const std::vector<std::optional<std::size_t>> below =
			CarryBackward(depths, blank, DepthBelowRange(buffer, lastLine, tabWidth));

		for (std::size_t index = 0; index < rangeSize; ++index)
		{
			if (!blank[index])
			{
				continue;
			}

			// A blank line belongs to neither the block above nor the one below, so a guide
			// is only honest at the shallower of the two; a missing side counts as depth 0,
			// which folds into the same minimum rather than needing its own case.
			depths[index] = std::min(above[index].value_or(0), below[index].value_or(0));
		}

		return depths;
	}
}