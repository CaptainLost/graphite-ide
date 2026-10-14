#include "Git/Diff/SyntaxRuns.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/ColourLines.h"

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr char kLineFeed = '\n';
		constexpr char kCarriageReturn = '\r';
		constexpr std::size_t kStartOfLine = 0;

		SyntaxRun PlainRun(std::size_t from, std::size_t to)
		{
			return SyntaxRun{.From = from, .To = to, .Kind = std::nullopt};
		}
	}

	std::string_view StripLineTerminator(std::string_view line)
	{
		if (!line.empty() && line.back() == kLineFeed)
		{
			line.remove_suffix(1);
		}

		if (!line.empty() && line.back() == kCarriageReturn)
		{
			line.remove_suffix(1);
		}

		return line;
	}

	std::vector<std::string_view> SplitTextLines(std::string_view text)
	{
		std::vector<std::string_view> lines;

		if (text.empty())
		{
			return lines;
		}

		std::size_t lineStart = 0;

		while (lineStart <= text.size())
		{
			const std::size_t newlineOffset = text.find(kLineFeed, lineStart);
			const bool isLastLine = newlineOffset == std::string_view::npos;
			const std::string_view line = isLastLine ? text.substr(lineStart)
													 : text.substr(lineStart, newlineOffset - lineStart);

			lines.push_back(StripLineTerminator(line));

			if (isLastLine)
			{
				break;
			}

			lineStart = newlineOffset + 1;

			const bool textEndsWithNewline = lineStart == text.size();

			if (textEndsWithNewline)
			{
				break;
			}
		}

		return lines;
	}

	std::vector<std::vector<Token>> ColourFileText(std::string_view text, const std::filesystem::path& path)
	{
		const std::vector<std::string_view> lines = SplitTextLines(text);

		return ColourLines(LanguageOfPath(path), lines);
	}

	// WARNING: Always returns at least one run, even for an empty line: a row that draws no run
	// sizes no item, so the cursor never advances and the next row lands on top of it.
	std::vector<SyntaxRun> RunsFor(std::string_view line, const std::vector<Token>* tokens)
	{
		std::vector<SyntaxRun> runs;

		if (tokens == nullptr || tokens->empty())
		{
			runs.push_back(PlainRun(kStartOfLine, line.size()));
			return runs;
		}

		std::size_t cursor = 0;

		for (const Token& token : *tokens)
		{
			if (cursor >= line.size())
			{
				break;
			}

			const std::size_t clampedFrom = std::min(token.From, line.size());
			const std::size_t clampedTo = std::min(token.To, line.size());

			if (clampedFrom > cursor)
			{
				runs.push_back(PlainRun(cursor, clampedFrom));
			}

			if (clampedTo > clampedFrom)
			{
				runs.push_back(SyntaxRun{.From = clampedFrom, .To = clampedTo, .Kind = token.Kind});
			}

			cursor = clampedTo;
		}

		if (cursor < line.size())
		{
			runs.push_back(PlainRun(cursor, line.size()));
		}

		if (runs.empty())
		{
			runs.push_back(PlainRun(kStartOfLine, line.size()));
		}

		return runs;
	}
}
