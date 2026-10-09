#include "Language/Syntax/ColourLines.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxTree.h"

#include <string>

namespace Graphite {

	namespace {

		std::vector<std::vector<Token>> LexLines(const Lexer& lexer, std::span<const std::string_view> lines)
		{
			std::vector<std::vector<Token>> tokensOfLines;
			LineState state;

			for (const std::string_view line : lines)
			{
				std::vector<Token> tokens;
				state = lexer.LexLine(line, state, tokens);
				tokensOfLines.push_back(std::move(tokens));
			}

			return tokensOfLines;
		}

		std::string JoinLines(std::span<const std::string_view> lines)
		{
			std::string text;

			for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
			{
				if (lineIndex > 0)
				{
					text += '\n';
				}

				text += lines[lineIndex];
			}

			return text;
		}

		std::vector<std::vector<Token>> ParseLines(const Grammar& grammar, std::span<const std::string_view> lines)
		{
			SyntaxTree tree(grammar);
			tree.Retarget(JoinLines(lines));
			tree.AdvanceToEnd();

			std::vector<std::vector<Token>> tokensOfLines(lines.size());

			for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
			{
				tree.AppendRowTokens(lineIndex, tokensOfLines[lineIndex]);
			}

			return tokensOfLines;
		}
	}

	std::vector<std::vector<Token>> ColourLines(LanguageId language, std::span<const std::string_view> lines)
	{
		const SyntaxSource source = SyntaxOf(language);

		if (lines.empty())
		{
			return {};
		}

		if (source.LineLexer != nullptr)
		{
			return LexLines(*source.LineLexer, lines);
		}

		if (source.TreeGrammar != nullptr)
		{
			return ParseLines(*source.TreeGrammar, lines);
		}

		return {};
	}
}
