#include "corepch.h"

#include "Core/Text/Indent.h"

#include "Core/Base/Ascii.h"
#include "Core/Jobs/CancellationPacing.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <optional>

namespace Core {

	namespace {
		constexpr std::size_t kDepthOfMissingNeighbour = 0;
		constexpr std::size_t kFirstIndentPrefixBytes = 64;

		bool IsIndentWhitespace(char character)
		{
			return character == ' ' || character == '\t';
		}

		struct LineIndent
		{
			bool IsBlank = true;
			std::size_t Depth = 0;
		};

		// WARNING: Reads a prefix that doubles until it holds text, so a giant line is not copied for its indent.
		LineIndent IndentOfLine(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
		{
			const std::size_t lineStart = buffer.LineStart(line);
			const std::size_t lineLength = buffer.LineLength(line);
			std::size_t prefixLength = std::min(lineLength, kFirstIndentPrefixBytes);

			while (true)
			{
				const std::string prefix = buffer.Substring(lineStart, lineStart + prefixLength);

				if (!ContainsOnlySpacesAndTabs(prefix))
				{
					return LineIndent{.IsBlank = false, .Depth = IndentDepth(prefix, tabWidth)};
				}

				if (prefixLength == lineLength)
				{
					return LineIndent{};
				}

				prefixLength = std::min(lineLength, prefixLength * 2);
			}
		}

		std::optional<std::size_t> DepthAboveRange(const TextBuffer& buffer, std::size_t firstLine,
												   std::size_t tabWidth)
		{
			for (std::size_t linesLeftAbove = firstLine; linesLeftAbove > 0; --linesLeftAbove)
			{
				const LineIndent indent = IndentOfLine(buffer, linesLeftAbove - 1, tabWidth);

				if (!indent.IsBlank)
				{
					return indent.Depth;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> DepthBelowRange(const TextBuffer& buffer, std::size_t lastLine,
												   std::size_t tabWidth)
		{
			for (std::size_t lineIndex = lastLine + 1; lineIndex < buffer.GetLineCount(); ++lineIndex)
			{
				const LineIndent indent = IndentOfLine(buffer, lineIndex, tabWidth);

				if (!indent.IsBlank)
				{
					return indent.Depth;
				}
			}

			return std::nullopt;
		}

		std::vector<std::optional<std::size_t>> CarryForward(
			const std::vector<std::size_t>& depths, const std::vector<bool>& isBlankLine,
			std::optional<std::size_t> nearestNonBlankDepth)
		{
			std::vector<std::optional<std::size_t>> carriedDepths(depths.size());

			for (std::size_t rangeIndex = 0; rangeIndex < depths.size(); ++rangeIndex)
			{
				if (!isBlankLine[rangeIndex])
				{
					nearestNonBlankDepth = depths[rangeIndex];
				}

				carriedDepths[rangeIndex] = nearestNonBlankDepth;
			}

			return carriedDepths;
		}

		std::vector<std::optional<std::size_t>> CarryBackward(
			const std::vector<std::size_t>& depths, const std::vector<bool>& isBlankLine,
			std::optional<std::size_t> nearestNonBlankDepth)
		{
			std::vector<std::optional<std::size_t>> carriedDepths(depths.size());

			for (std::size_t linesLeft = depths.size(); linesLeft > 0; --linesLeft)
			{
				const std::size_t rangeIndex = linesLeft - 1;

				if (!isBlankLine[rangeIndex])
				{
					nearestNonBlankDepth = depths[rangeIndex];
				}

				carriedDepths[rangeIndex] = nearestNonBlankDepth;
			}

			return carriedDepths;
		}
	}

	std::size_t IndentDepth(std::string_view lineText, std::size_t tabWidth)
	{
		std::size_t indentEnd = 0;

		while (indentEnd < lineText.size() && IsIndentWhitespace(lineText[indentEnd]))
		{
			++indentEnd;
		}

		const bool lineIsOnlyIndentation = indentEnd == lineText.size();

		if (lineIsOnlyIndentation)
		{
			return 0;
		}

		return ColumnOfByte(lineText, indentEnd, tabWidth);
	}

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer, std::size_t tabWidth)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		const std::size_t firstLine = 0;
		const std::size_t lastLine = lineCount - 1;

		return IndentDepths(buffer, tabWidth, firstLine, lastLine);
	}

	std::vector<std::size_t> IndentDepths(const TextBuffer& buffer, std::size_t tabWidth,
										  std::size_t firstLine, std::size_t lastLine)
	{
		const CancellationToken neverCancelled;

		return *IndentDepthsUnlessCancelled(buffer, tabWidth, firstLine, lastLine, neverCancelled);
	}

	std::optional<std::vector<std::size_t>> IndentDepthsUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
																		const CancellationToken& cancellation)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		const std::size_t firstLine = 0;
		const std::size_t lastLine = lineCount - 1;

		return IndentDepthsUnlessCancelled(buffer, tabWidth, firstLine, lastLine, cancellation);
	}

	std::optional<std::vector<std::size_t>> IndentDepthsUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
																		std::size_t firstLine, std::size_t lastLine,
																		const CancellationToken& cancellation)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		firstLine = std::min(firstLine, lineCount - 1);
		lastLine = std::min(lastLine, lineCount - 1);

		const std::size_t rangeLineCount = lastLine - firstLine + 1;
		std::vector<std::size_t> depths(rangeLineCount);
		std::vector<bool> isBlankLine(rangeLineCount);

		CancellationPacing pacing{cancellation};

		for (std::size_t lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex)
		{
			if (pacing.CancelledBefore(buffer.LineLength(lineIndex)))
			{
				return std::nullopt;
			}

			const LineIndent indent = IndentOfLine(buffer, lineIndex, tabWidth);
			const std::size_t rangeIndex = lineIndex - firstLine;
			isBlankLine[rangeIndex] = indent.IsBlank;
			depths[rangeIndex] = indent.Depth;
		}

		const std::vector<std::optional<std::size_t>> nearestDepthAbove =
			CarryForward(depths, isBlankLine, DepthAboveRange(buffer, firstLine, tabWidth));

		const std::vector<std::optional<std::size_t>> nearestDepthBelow =
			CarryBackward(depths, isBlankLine, DepthBelowRange(buffer, lastLine, tabWidth));

		for (std::size_t rangeIndex = 0; rangeIndex < rangeLineCount; ++rangeIndex)
		{
			if (!isBlankLine[rangeIndex])
			{
				continue;
			}

			const std::size_t depthAbove = nearestDepthAbove[rangeIndex].value_or(kDepthOfMissingNeighbour);
			const std::size_t depthBelow = nearestDepthBelow[rangeIndex].value_or(kDepthOfMissingNeighbour);
			depths[rangeIndex] = std::min(depthAbove, depthBelow);
		}

		return depths;
	}
}