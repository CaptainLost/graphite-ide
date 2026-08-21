#include "Git/SyntaxRuns.h"

#include "Lang/Registry/LanguageRegistry.h"

namespace Graphite {

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

			// A CRLF file leaves the '\r' as the last byte of the line the split above produces;
			// strip it so it never ends up inside the last token's span.
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}

			lines.push_back(line);

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
}
