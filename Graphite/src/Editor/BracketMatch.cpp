#include "Editor/BracketMatch.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Language/Api/TokenKind.h"
#include "Language/Syntax/SyntaxCache.h"
#include "Language/Syntax/Token.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		bool IsText(TokenKind kind)
		{
			return kind == TokenKind::String || kind == TokenKind::Character || kind == TokenKind::Comment || kind == TokenKind::Preprocessor;
		}

		bool IsCode(TokenKind kind)
		{
			return !IsText(kind);
		}

		bool IsOpen(char bracket)
		{
			return bracket == '(' || bracket == '[' || bracket == '{';
		}

		bool IsClose(char bracket)
		{
			return bracket == ')' || bracket == ']' || bracket == '}';
		}

		// WARNING: Angle brackets are left out on purpose. `a < b` and `vector<int>` are the same two characters, and telling them apart needs a parser rather than a scan.
		char PartnerOf(char bracket)
		{
			switch (bracket)
			{
			case '(':
				return ')';
			case ')':
				return '(';
			case '[':
				return ']';
			case ']':
				return '[';
			case '{':
				return '}';
			case '}':
				return '{';
			default:
				return '\0';
			}
		}

		// WARNING: Cached tokens are offsets into the tab-expanded line, so the raw byte is expanded with ExpandedOffsetOfByte. ColumnOfByte counts a column per character and agrees only on ASCII.
		TokenKind KindAt(SyntaxCache& syntax, const Core::TextBuffer& buffer, std::size_t line,
						 std::string_view lineText, std::size_t byteInLine, std::size_t tabWidth)
		{
			const std::size_t expandedOffset =
				Core::ExpandedOffsetOfByte(lineText, byteInLine, tabWidth);

			for (const Token& token : syntax.TokensOf(buffer, line))
			{
				if (expandedOffset >= token.From && expandedOffset < token.To)
				{
					return token.Kind;
				}
			}

			return TokenKind::Text;
		}

		std::optional<char> BracketAt(const Core::TextBuffer& buffer, SyntaxCache& syntax,
									  std::size_t offset, std::size_t tabWidth)
		{
			if (offset >= buffer.GetLength())
			{
				return std::nullopt;
			}

			const char bracket = buffer.CharAt(offset);

			if (!IsOpen(bracket) && !IsClose(bracket))
			{
				return std::nullopt;
			}

			const Core::Position position = buffer.ToPosition(offset);
			const std::string lineText = buffer.LineText(position.Line);

			if (!IsCode(
					KindAt(syntax, buffer, position.Line, lineText, position.Column, tabWidth)))
			{
				return std::nullopt;
			}

			return bracket;
		}

		std::optional<std::size_t> ScanForward(const Core::TextBuffer& buffer, SyntaxCache& syntax,
											   std::size_t firstOffset, char open, std::size_t tabWidth)
		{
			const char close = PartnerOf(open);
			const std::size_t startLine = buffer.ToPosition(firstOffset).Line;
			const std::size_t lastLine =
				std::min(buffer.GetLineCount() - 1, startLine + kBracketScanLines);

			std::size_t depth = 1;

			for (std::size_t line = startLine; line <= lastLine; ++line)
			{
				const std::string lineText = buffer.LineText(line);
				const std::size_t lineStart = buffer.LineStart(line);
				const std::size_t firstByte = line == startLine ? firstOffset - lineStart : 0;

				for (std::size_t byteInLine = firstByte; byteInLine < lineText.size(); ++byteInLine)
				{
					const char bracket = lineText[byteInLine];

					if (bracket != open && bracket != close)
					{
						continue;
					}

					if (!IsCode(KindAt(syntax, buffer, line, lineText, byteInLine, tabWidth)))
					{
						continue;
					}

					if (bracket == open)
					{
						++depth;
						continue;
					}

					if (--depth == 0)
					{
						return lineStart + byteInLine;
					}
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> ScanBackward(const Core::TextBuffer& buffer, SyntaxCache& syntax,
												std::size_t closeOffset, char close, std::size_t tabWidth)
		{
			const char open = PartnerOf(close);
			const Core::Position closePosition = buffer.ToPosition(closeOffset);
			const std::size_t firstLine =
				closePosition.Line > kBracketScanLines ? closePosition.Line - kBracketScanLines : 0;

			std::size_t depth = 1;

			for (std::size_t line = closePosition.Line + 1; line-- > firstLine;)
			{
				const std::string lineText = buffer.LineText(line);
				const std::size_t lineStart = buffer.LineStart(line);
				const std::size_t endByte =
					line == closePosition.Line ? std::min(closePosition.Column, lineText.size()) : lineText.size();

				for (std::size_t byteInLine = endByte; byteInLine-- > 0;)
				{
					const char bracket = lineText[byteInLine];

					if (bracket != open && bracket != close)
					{
						continue;
					}

					if (!IsCode(KindAt(syntax, buffer, line, lineText, byteInLine, tabWidth)))
					{
						continue;
					}

					if (bracket == close)
					{
						++depth;
						continue;
					}

					if (--depth == 0)
					{
						return lineStart + byteInLine;
					}
				}
			}

			return std::nullopt;
		}
	}

	std::optional<BracketPair> FindBracketPair(const Core::TextBuffer& buffer, SyntaxCache& syntax,
											   std::size_t caret)
	{
		// WARNING: The tab width comes from the cache, never a parameter. A width that disagreed with the one the tokens were produced with would map bytes onto the wrong token.
		const std::size_t tabWidth = syntax.GetTabWidth();

		std::size_t bracketOffset = caret;
		std::optional<char> bracket = BracketAt(buffer, syntax, caret, tabWidth);

		if (!bracket && caret > 0)
		{
			bracketOffset = caret - 1;
			bracket = BracketAt(buffer, syntax, bracketOffset, tabWidth);
		}

		if (!bracket)
		{
			return std::nullopt;
		}

		if (IsOpen(*bracket))
		{
			const std::optional<std::size_t> close =
				ScanForward(buffer, syntax, bracketOffset + 1, *bracket, tabWidth);

			if (close)
			{
				return BracketPair{.Open = bracketOffset, .Close = *close, .Matched = true};
			}
		}
		else
		{
			const std::optional<std::size_t> open =
				ScanBackward(buffer, syntax, bracketOffset, *bracket, tabWidth);

			if (open)
			{
				return BracketPair{.Open = *open, .Close = bracketOffset, .Matched = true};
			}
		}

		return BracketPair{.Open = bracketOffset, .Close = bracketOffset, .Matched = false};
	}
}
