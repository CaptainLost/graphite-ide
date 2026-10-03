#include "Editor/BracketMatch.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Lang/Api/TokenKind.h"
#include "Lang/Syntax/SyntaxCache.h"
#include "Lang/Syntax/Token.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		// Everything the lexer calls text rather than code. A bracket in one of these is a
		// character of prose, not a delimiter, so neither end of a pair may sit in one and the
		// partner search reads straight past them.
		bool IsCode(TokenKind kind)
		{
			return kind != TokenKind::String && kind != TokenKind::Character && kind != TokenKind::Comment && kind != TokenKind::Preprocessor;
		}

		bool IsOpen(char bracket)
		{
			return bracket == '(' || bracket == '[' || bracket == '{';
		}

		bool IsClose(char bracket)
		{
			return bracket == ')' || bracket == ']' || bracket == '}';
		}

		// Only the three shapes that are always delimiters. Angle brackets are left out on
		// purpose: `a < b` and `vector<int>` are the same two characters, and telling them apart
		// needs a parser rather than a scan.
		char Opposite(char bracket)
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

		// What the lexer made of the byte at `byteInLine`. Tokens the cache holds are offsets
		// into the TAB-EXPANDED line, so the raw byte has to be expanded before it can be
		// compared against a range — ExpandedOffsetOfByte and not ColumnOfByte, which counts a
		// column per character and so only agrees with the expanded string on ASCII.
		//
		// A byte in a gap between tokens is whitespace, which is code for this purpose.
		TokenKind KindAt(SyntaxCache& syntax, const Core::TextBuffer& buffer, std::size_t line,
						 std::string_view lineText, std::size_t byteInLine, std::size_t tabWidth)
		{
			const std::size_t expanded =
				Core::ExpandedOffsetOfByte(lineText, byteInLine, tabWidth);

			for (const Token& token : syntax.TokensOf(buffer, line))
			{
				if (expanded >= token.From && expanded < token.To)
				{
					return token.Kind;
				}
			}

			return TokenKind::Text;
		}

		// The bracket at `offset`, or nothing when that byte is not one or is inside a string,
		// character, comment or preprocessor line.
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

		// The offset of the bracket closing the `open` one, reading forwards from `from` — the
		// byte just past that opening bracket. Depth starts at one and the answer is the bracket
		// that brings it back to zero, so a nested pair of the same shape does not steal it.
		std::optional<std::size_t> ScanForward(const Core::TextBuffer& buffer, SyntaxCache& syntax,
											   std::size_t from, char open, std::size_t tabWidth)
		{
			const char close = Opposite(open);
			const std::size_t startLine = buffer.ToPosition(from).Line;
			const std::size_t lastLine =
				std::min(buffer.GetLineCount() - 1, startLine + kBracketScanLines);

			std::size_t depth = 1;

			for (std::size_t line = startLine; line <= lastLine; ++line)
			{
				const std::string lineText = buffer.LineText(line);
				const std::size_t lineStart = buffer.LineStart(line);
				const std::size_t first = line == startLine ? from - lineStart : 0;

				for (std::size_t at = first; at < lineText.size(); ++at)
				{
					const char bracket = lineText[at];

					if (bracket != open && bracket != close)
					{
						continue;
					}

					if (!IsCode(KindAt(syntax, buffer, line, lineText, at, tabWidth)))
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
						return lineStart + at;
					}
				}
			}

			return std::nullopt;
		}

		// The mirror of ScanForward: `end` is the offset of the closing bracket itself, and the
		// scan reads the bytes strictly before it, backwards.
		std::optional<std::size_t> ScanBackward(const Core::TextBuffer& buffer, SyntaxCache& syntax,
												std::size_t end, char close, std::size_t tabWidth)
		{
			const char open = Opposite(close);
			const Core::Position start = buffer.ToPosition(end);
			const std::size_t firstLine =
				start.Line > kBracketScanLines ? start.Line - kBracketScanLines : 0;

			std::size_t depth = 1;

			for (std::size_t line = start.Line + 1; line-- > firstLine;)
			{
				const std::string lineText = buffer.LineText(line);
				const std::size_t lineStart = buffer.LineStart(line);
				const std::size_t past =
					line == start.Line ? std::min(start.Column, lineText.size()) : lineText.size();

				for (std::size_t at = past; at-- > 0;)
				{
					const char bracket = lineText[at];

					if (bracket != open && bracket != close)
					{
						continue;
					}

					if (!IsCode(KindAt(syntax, buffer, line, lineText, at, tabWidth)))
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
						return lineStart + at;
					}
				}
			}

			return std::nullopt;
		}
	}

	std::optional<BracketPair> FindBracketPair(const Core::TextBuffer& buffer, SyntaxCache& syntax,
											   std::size_t caret)
	{
		const std::size_t tabWidth = syntax.GetTabWidth();

		// The bracket after the caret wins; only when there is none does the one before it
		// count, which is what puts a highlight on the `)` a caret has just been typed past.
		std::size_t at = caret;
		std::optional<char> bracket = BracketAt(buffer, syntax, caret, tabWidth);

		if (!bracket && caret > 0)
		{
			at = caret - 1;
			bracket = BracketAt(buffer, syntax, at, tabWidth);
		}

		if (!bracket)
		{
			return std::nullopt;
		}

		if (IsOpen(*bracket))
		{
			const std::optional<std::size_t> close =
				ScanForward(buffer, syntax, at + 1, *bracket, tabWidth);

			if (close)
			{
				return BracketPair{at, *close, true};
			}
		}
		else
		{
			const std::optional<std::size_t> open =
				ScanBackward(buffer, syntax, at, *bracket, tabWidth);

			if (open)
			{
				return BracketPair{*open, at, true};
			}
		}

		// On a bracket, but its partner is missing or further away than the scan reads. Both
		// offsets are the bracket's own, and the caller draws it in the unmatched colour.
		return BracketPair{at, at, false};
	}
}
