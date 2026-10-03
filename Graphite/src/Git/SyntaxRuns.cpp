#include "Git/SyntaxRuns.h"

#include "Lang/Registry/LanguageRegistry.h"

#include <algorithm>

namespace Graphite {

	std::string_view StripLineTerminator(std::string_view line)
	{
		if (!line.empty() && line.back() == '\n')
		{
			line.remove_suffix(1);
		}

		// A CRLF file leaves the '\r' as the last byte once the '\n' is gone; strip it so it
		// never ends up inside the last token's span or as a stray glyph at the end of a drawn
		// row.
		if (!line.empty() && line.back() == '\r')
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

		std::size_t start = 0;

		while (start <= text.size())
		{
			const std::size_t newline = text.find('\n', start);
			std::string_view line = newline == std::string_view::npos
										? text.substr(start)
										: text.substr(start, newline - start);

			// The split above already consumed the '\n'; this takes the '\r' a CRLF file leaves
			// behind, by the same rule a diff line's own terminator is stripped with.
			lines.push_back(StripLineTerminator(line));

			if (newline == std::string_view::npos)
			{
				break;
			}

			start = newline + 1;

			// A trailing newline ends the text, not a further empty line after it.
			if (start == text.size())
			{
				break;
			}
		}

		return lines;
	}

	std::vector<std::vector<Token>> LexAllLines(std::string_view text, const Lexer& lexer)
	{
		std::vector<std::vector<Token>> result;
		LineState state;

		for (std::string_view line : SplitTextLines(text))
		{
			std::vector<Token> tokens;
			state = lexer.LexLine(line, state, tokens);
			result.push_back(std::move(tokens));
		}

		return result;
	}

	std::vector<std::vector<Token>> LexFileText(std::string_view text,
												const std::filesystem::path& path)
	{
		const Lexer* lexer = LexerFor(LanguageOfPath(path));

		if (lexer == nullptr)
		{
			return {};
		}

		return LexAllLines(text, *lexer);
	}

	std::vector<SyntaxRun> RunsFor(std::string_view line, const std::vector<Token>* tokens)
	{
		std::vector<SyntaxRun> runs;

		if (tokens == nullptr || tokens->empty())
		{
			runs.push_back(SyntaxRun{0, line.size(), std::nullopt});
			return runs;
		}

		std::size_t cursor = 0;

		for (const Token& token : *tokens)
		{
			if (cursor >= line.size())
			{
				break;
			}

			// Clamped rather than trusted — see the header.
			const std::size_t from = std::min(token.From, line.size());
			const std::size_t to = std::min(token.To, line.size());

			if (from > cursor)
			{
				runs.push_back(SyntaxRun{cursor, from, std::nullopt});
			}

			if (to > from)
			{
				runs.push_back(SyntaxRun{from, to, token.Kind});
			}

			cursor = to;
		}

		if (cursor < line.size())
		{
			runs.push_back(SyntaxRun{cursor, line.size(), std::nullopt});
		}

		// An empty line, or a token vector whose every token covers nothing, leaves the loop
		// above with nothing to show. One run over the whole line is what keeps the row sized —
		// see the header.
		if (runs.empty())
		{
			runs.push_back(SyntaxRun{0, line.size(), std::nullopt});
		}

		return runs;
	}
}
