#include "Git/SyntaxRuns.h"

#include "Lang/Registry/LanguageRegistry.h"

namespace Graphite {

	std::vector<std::vector<Token>> LexAllLines(std::string_view text, const Lexer& lexer)
	{
		std::vector<std::vector<Token>> result;

		if (text.empty())
		{
			return result;
		}

		LineState state;
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

			std::vector<Token> tokens;
			state = lexer.LexLine(line, state, tokens);
			result.push_back(std::move(tokens));

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
