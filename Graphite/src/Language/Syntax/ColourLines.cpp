#include "Language/Syntax/ColourLines.h"

#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxTree.h"

#include <cstddef>
#include <iterator>
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

		constexpr std::string_view kBlanks = " \t";

		std::string_view FirstWordOf(std::string_view line)
		{
			const std::size_t wordStart = line.find_first_not_of(kBlanks);

			if (wordStart == std::string_view::npos)
			{
				return {};
			}

			const std::size_t wordEnd = line.find_first_of(kBlanks, wordStart);

			return line.substr(wordStart, wordEnd == std::string_view::npos ? std::string_view::npos : wordEnd - wordStart);
		}

		bool DeclaresAtTopLevel(const MemberContext& context, std::string_view firstLine)
		{
			const std::string_view firstWord = FirstWordOf(firstLine);

			for (const std::string_view keyword : context.TopLevelKeywords)
			{
				if (firstWord == keyword)
				{
					return true;
				}
			}

			return false;
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

	std::vector<std::vector<Token>> ColourDeclaration(LanguageId language, std::span<const std::string_view> lines)
	{
		const MemberContext& context = DefinitionOf(language).DeclarationContext;

		if (context.Opening.empty() || lines.empty() || DeclaresAtTopLevel(context, lines.front()))
		{
			return ColourLines(language, lines);
		}

		std::vector<std::string_view> wrapped;
		wrapped.reserve(lines.size() + 2);
		wrapped.push_back(context.Opening);
		wrapped.insert(wrapped.end(), lines.begin(), lines.end());
		wrapped.push_back(context.Closing);

		std::vector<std::vector<Token>> colouredWrapped = ColourLines(language, wrapped);

		if (colouredWrapped.size() != wrapped.size())
		{
			return {};
		}

		return std::vector<std::vector<Token>>(std::make_move_iterator(colouredWrapped.begin() + 1),
											   std::make_move_iterator(colouredWrapped.end() - 1));
	}
}
