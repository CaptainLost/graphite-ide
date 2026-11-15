#include "corepch.h"

#include "Core/Text/Typing.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace Core {

	namespace {
		constexpr char kNoCharacter = '\0';
		constexpr char kEscape = '\\';
		constexpr std::string_view kOpeningBrackets = "([{";
		constexpr std::string_view kClosingBrackets = ")]}";
		constexpr std::string_view kQuotes = "\"'";
		constexpr std::string_view kLineComment = "//";

		// What may follow an opening bracket or quote for it to get its partner: closing a pair right before a word
		// would wrap nothing and leave a stray closer behind the word.
		constexpr std::string_view kClosesBefore = ";:.,=)]}>";

		// How far back a closing bracket typed on its own line looks for the line it closes.
		constexpr std::size_t kPartnerScanLines = 2000;

		constexpr std::size_t kFirstMultiByte = 0x80;

		bool IsQuote(char character)
		{
			return character != kNoCharacter && kQuotes.find(character) != std::string_view::npos;
		}

		bool IsOpeningBracket(char character)
		{
			return character != kNoCharacter && kOpeningBrackets.find(character) != std::string_view::npos;
		}

		bool IsClosingBracket(char character)
		{
			return character != kNoCharacter && kClosingBrackets.find(character) != std::string_view::npos;
		}

		char OpeningPartnerOf(char closing)
		{
			const std::size_t index = kClosingBrackets.find(closing);

			return index == std::string_view::npos ? kNoCharacter : kOpeningBrackets[index];
		}

		bool IsWordByte(char character)
		{
			const auto byte = static_cast<unsigned char>(character);

			return byte >= kFirstMultiByte || std::isalnum(byte) != 0 || character == '_';
		}

		bool IsIndentWhitespace(char character)
		{
			return character == ' ' || character == '\t';
		}

		std::string_view LeadingWhitespaceOf(std::string_view lineText)
		{
			const std::size_t indentEnd = lineText.find_first_not_of(" \t");

			return lineText.substr(0, indentEnd == std::string_view::npos ? lineText.size() : indentEnd);
		}

		std::string_view WithoutTrailingWhitespace(std::string_view text)
		{
			const std::size_t lastKept = text.find_last_not_of(" \t");

			return lastKept == std::string_view::npos ? std::string_view{} : text.substr(0, lastKept + 1);
		}

		std::size_t TabWidthOf(IndentStyle style)
		{
			return style.Width == 0 ? DefaultTabWidth : style.Width;
		}

		std::string WhitespaceOfColumns(std::size_t columns, bool useTabs, std::size_t tabWidth)
		{
			if (!useTabs)
			{
				return std::string(columns, ' ');
			}

			return std::string(columns / tabWidth, '\t') + std::string(columns % tabWidth, ' ');
		}

		char CharacterBefore(const TextBuffer& buffer, std::size_t offset, std::size_t lineStart)
		{
			return offset > lineStart ? buffer.CharAt(offset - 1) : kNoCharacter;
		}

		char CharacterAfter(const TextBuffer& buffer, std::size_t offset, std::size_t lineEnd)
		{
			return offset < lineEnd ? buffer.CharAt(offset) : kNoCharacter;
		}

		bool MayCloseBefore(char next)
		{
			return next == kNoCharacter || IsAsciiWhitespace(next) ||
				   kClosesBefore.find(next) != std::string_view::npos;
		}

		// Whether the text before the caret leaves a string of this quote open: an odd count of the quote,
		// leaving out the escaped ones.
		bool InsideStringOf(std::string_view lineBeforeCaret, char quote)
		{
			bool inside = false;

			for (std::size_t index = 0; index < lineBeforeCaret.size(); ++index)
			{
				if (lineBeforeCaret[index] == kEscape)
				{
					++index;
					continue;
				}

				if (lineBeforeCaret[index] == quote)
				{
					inside = !inside;
				}
			}

			return inside;
		}

		// The brackets of a line outside its strings and its line comment, in order. A guess, not a parse: block
		// comments and the strings of other languages fool it, which costs a closing bracket its indentation and
		// nothing else.
		std::string CodeBracketsOf(std::string_view lineText)
		{
			std::string brackets;
			char openQuote = kNoCharacter;

			for (std::size_t index = 0; index < lineText.size(); ++index)
			{
				const char character = lineText[index];

				if (openQuote != kNoCharacter)
				{
					if (character == kEscape)
					{
						++index;
					}
					else if (character == openQuote)
					{
						openQuote = kNoCharacter;
					}

					continue;
				}

				if (lineText.substr(index).starts_with(kLineComment))
				{
					break;
				}

				if (IsQuote(character))
				{
					openQuote = character;
				}
				else if (IsOpeningBracket(character) || IsClosingBracket(character))
				{
					brackets.push_back(character);
				}
			}

			return brackets;
		}

		std::optional<std::size_t> LineOpening(const TextBuffer& buffer, std::size_t closingLine, char closing)
		{
			const char opening = OpeningPartnerOf(closing);
			std::size_t depth = 0;
			const std::size_t lowestLine = closingLine > kPartnerScanLines ? closingLine - kPartnerScanLines : 0;

			for (std::size_t line = closingLine; line > lowestLine;)
			{
				--line;

				const std::string brackets = CodeBracketsOf(buffer.LineText(line));

				for (std::size_t left = brackets.size(); left > 0; --left)
				{
					const char bracket = brackets[left - 1];

					if (bracket == closing)
					{
						++depth;
					}
					else if (bracket == opening)
					{
						if (depth == 0)
						{
							return line;
						}

						--depth;
					}
				}
			}

			return std::nullopt;
		}

		void PlaceCaret(Caret& caret, std::size_t anchor, std::size_t offset)
		{
			caret.Anchor = anchor;
			caret.Offset = offset;
			caret.DesiredColumn = NoDesiredColumn;
		}

		bool Surround(TextBuffer& buffer, Caret& caret, char opening, char closing)
		{
			const auto [from, to] = SelectionRange(caret);
			const bool caretAtEnd = caret.Offset == to;

			buffer.BreakUndoGroup();
			buffer.BeginTransaction();
			buffer.Insert(to, std::string(1, closing));
			buffer.Insert(from, std::string(1, opening));
			buffer.EndTransaction();
			buffer.BreakUndoGroup();

			const std::size_t innerFrom = from + 1;
			const std::size_t innerTo = to + 1;

			PlaceCaret(caret, caretAtEnd ? innerFrom : innerTo, caretAtEnd ? innerTo : innerFrom);

			return true;
		}

		bool InsertPair(TextBuffer& buffer, Caret& caret, char opening, char closing)
		{
			const std::size_t at = caret.Offset;

			buffer.BeginTransaction();
			buffer.Insert(at, std::string{opening, closing});
			buffer.EndTransaction();

			PlaceCaret(caret, at + 1, at + 1);

			return true;
		}

		bool CloseOnOwnLine(TextBuffer& buffer, Caret& caret, std::size_t lineStart, std::string_view indent,
							char closing)
		{
			const std::size_t at = caret.Offset;

			buffer.BeginTransaction();
			buffer.Erase(lineStart, at);
			buffer.Insert(lineStart, std::string{indent} + closing);
			buffer.EndTransaction();

			const std::size_t afterClosing = lineStart + indent.size() + 1;
			PlaceCaret(caret, afterClosing, afterClosing);

			return true;
		}

		std::vector<std::string_view> SplitLines(std::string_view text)
		{
			std::vector<std::string_view> lines;
			std::size_t start = 0;

			while (true)
			{
				const std::size_t newline = text.find('\n', start);

				if (newline == std::string_view::npos)
				{
					lines.push_back(text.substr(start));
					return lines;
				}

				lines.push_back(text.substr(start, newline - start));
				start = newline + 1;
			}
		}

		std::string WithEveryLineEndingAsNewline(std::string_view text)
		{
			std::string withNewlines;
			withNewlines.reserve(text.size());

			for (std::size_t index = 0; index < text.size(); ++index)
			{
				if (text[index] == '\r')
				{
					if (index + 1 < text.size() && text[index + 1] == '\n')
					{
						++index;
					}

					withNewlines.push_back('\n');
					continue;
				}

				withNewlines.push_back(text[index]);
			}

			return withNewlines;
		}

		// The indentation a blank line would take from the code above it.
		std::string IndentFromLinesAbove(const TextBuffer& buffer, std::size_t line, IndentStyle style)
		{
			for (std::size_t above = line; above > 0;)
			{
				--above;

				const std::string text = buffer.LineText(above);

				if (!ContainsOnlySpacesAndTabs(text))
				{
					return IndentAfterLine(text, style);
				}
			}

			return {};
		}
	}

	std::optional<char> ClosingPartnerOf(char opening)
	{
		if (IsQuote(opening))
		{
			return opening;
		}

		const std::size_t index = kOpeningBrackets.find(opening);

		if (opening == kNoCharacter || index == std::string_view::npos)
		{
			return std::nullopt;
		}

		return kClosingBrackets[index];
	}

	std::string IndentUnitFor(std::string_view existingIndent, IndentStyle style)
	{
		const bool fileUsesTabs = existingIndent.find('\t') != std::string_view::npos;
		const bool fileUsesSpaces = !existingIndent.empty() && !fileUsesTabs;
		const bool useTabs = fileUsesTabs || (!fileUsesSpaces && !style.UseSpaces);

		return useTabs ? std::string{"\t"} : std::string(TabWidthOf(style), ' ');
	}

	std::string IndentAfterLine(std::string_view lineText, IndentStyle style)
	{
		const std::string_view indent = LeadingWhitespaceOf(lineText);
		const std::string_view content = WithoutTrailingWhitespace(lineText);

		if (!content.empty() && IsOpeningBracket(content.back()))
		{
			return std::string{indent} + IndentUnitFor(indent, style);
		}

		return std::string{indent};
	}

	bool TypeCharacter(TextDocument& document, Caret& caret, std::string_view utf8)
	{
		if (document.IsReadOnly())
		{
			return false;
		}

		if (utf8.size() != 1)
		{
			return InsertText(document, caret, utf8);
		}

		TextBuffer& buffer = document.GetBuffer();
		const char typed = utf8.front();
		const std::optional<char> partner = ClosingPartnerOf(typed);

		if (HasSelection(caret))
		{
			return partner ? Surround(buffer, caret, typed, *partner) : InsertText(document, caret, utf8);
		}

		const std::size_t line = buffer.ToPosition(caret.Offset).Line;
		const std::size_t lineStart = buffer.LineStart(line);
		const std::size_t lineEnd = lineStart + buffer.LineLength(line);
		const std::string lineBeforeCaret = buffer.Substring(lineStart, caret.Offset);
		const char previous = CharacterBefore(buffer, caret.Offset, lineStart);
		const char next = CharacterAfter(buffer, caret.Offset, lineEnd);

		const bool stepsOverClosing =
			next == typed && (IsClosingBracket(typed) || (IsQuote(typed) && InsideStringOf(lineBeforeCaret, typed)));

		if (stepsOverClosing)
		{
			PlaceCaret(caret, caret.Offset + 1, caret.Offset + 1);
			buffer.BreakUndoGroup();

			return false;
		}

		if (IsOpeningBracket(typed) && MayCloseBefore(next))
		{
			return InsertPair(buffer, caret, typed, *partner);
		}

		if (IsQuote(typed))
		{
			const bool closesHere = MayCloseBefore(next) && !IsWordByte(previous) && previous != typed &&
									!InsideStringOf(lineBeforeCaret, typed);

			return closesHere ? InsertPair(buffer, caret, typed, typed) : InsertText(document, caret, utf8);
		}

		if (IsClosingBracket(typed) && ContainsOnlySpacesAndTabs(lineBeforeCaret))
		{
			if (const std::optional<std::size_t> openingLine = LineOpening(buffer, line, typed))
			{
				const std::string openingText = buffer.LineText(*openingLine);

				return CloseOnOwnLine(buffer, caret, lineStart, LeadingWhitespaceOf(openingText), typed);
			}
		}

		return InsertText(document, caret, utf8);
	}

	bool DeleteBackwardAcrossPair(TextDocument& document, Caret& caret, std::size_t tabWidth)
	{
		if (document.IsReadOnly() || HasSelection(caret) || caret.Offset == 0)
		{
			return DeleteBackward(document, caret, tabWidth);
		}

		TextBuffer& buffer = document.GetBuffer();
		const std::size_t at = caret.Offset;
		const std::optional<char> partner = ClosingPartnerOf(buffer.CharAt(at - 1));
		const bool betweenPair = partner && at < buffer.GetLength() && buffer.CharAt(at) == *partner;

		if (!betweenPair)
		{
			return DeleteBackward(document, caret, tabWidth);
		}

		buffer.BeginTransaction();
		buffer.Erase(at - 1, at + 1);
		buffer.EndTransaction();

		PlaceCaret(caret, at - 1, at - 1);

		return true;
	}

	std::string ReindentPasted(std::string_view pasted, std::string_view baseIndent, bool firstLineMoves,
							   IndentStyle style)
	{
		const std::vector<std::string_view> lines = SplitLines(pasted);
		const std::size_t tabWidth = TabWidthOf(style);

		std::size_t shallowest = std::numeric_limits<std::size_t>::max();

		for (std::size_t index = firstLineMoves ? 0 : 1; index < lines.size(); ++index)
		{
			if (!ContainsOnlySpacesAndTabs(lines[index]))
			{
				shallowest = std::min(shallowest, ColumnCount(LeadingWhitespaceOf(lines[index]), tabWidth));
			}
		}

		if (shallowest == std::numeric_limits<std::size_t>::max())
		{
			return std::string{pasted};
		}

		const bool useTabs = IndentUnitFor(baseIndent, style) == "\t";
		std::string reindented;
		reindented.reserve(pasted.size() + lines.size() * baseIndent.size());

		for (std::size_t index = 0; index < lines.size(); ++index)
		{
			const std::string_view line = lines[index];

			if (index == 0 && !firstLineMoves)
			{
				reindented += line;
			}
			else if (!ContainsOnlySpacesAndTabs(line))
			{
				const std::string_view indent = LeadingWhitespaceOf(line);
				const std::size_t deeper = ColumnCount(indent, tabWidth) - shallowest;

				reindented += baseIndent;
				reindented += WhitespaceOfColumns(deeper, useTabs, tabWidth);
				reindented += line.substr(indent.size());
			}

			if (index + 1 < lines.size())
			{
				reindented += '\n';
			}
		}

		return reindented;
	}

	bool PasteText(TextDocument& document, Caret& caret, std::string_view utf8, IndentStyle style)
	{
		if (document.IsReadOnly() || utf8.empty())
		{
			return false;
		}

		const std::string withNewlines = WithEveryLineEndingAsNewline(utf8);

		if (withNewlines.find('\n') == std::string::npos)
		{
			return InsertText(document, caret, withNewlines);
		}

		TextBuffer& buffer = document.GetBuffer();
		const auto [selectionStart, selectionEnd] = SelectionRange(caret);

		buffer.BreakUndoGroup();
		buffer.BeginTransaction();

		if (selectionStart != selectionEnd)
		{
			buffer.Erase(selectionStart, selectionEnd);
		}

		const std::size_t line = buffer.ToPosition(selectionStart).Line;
		const std::size_t lineStart = buffer.LineStart(line);
		const std::string lineText = buffer.LineText(line);
		const std::string_view lineIndent = LeadingWhitespaceOf(lineText);
		const bool atIndentation = selectionStart - lineStart <= lineIndent.size();
		const bool blankLine = lineIndent.size() == lineText.size();

		std::size_t replaceFrom = selectionStart;
		std::size_t replaceTo = selectionStart;
		std::string baseIndent{lineIndent};

		if (atIndentation)
		{
			// The whitespace in front of the caret is rebuilt with the first line; on a blank line the whole line
			// is, so no whitespace is left trailing behind what was pasted.
			replaceFrom = lineStart;
			replaceTo = blankLine ? lineStart + lineText.size() : selectionStart;

			if (blankLine && lineIndent.empty())
			{
				baseIndent = IndentFromLinesAbove(buffer, line, style);
			}
		}

		const std::string reindented = ReindentPasted(withNewlines, baseIndent, atIndentation, style);

		if (replaceTo != replaceFrom)
		{
			buffer.Erase(replaceFrom, replaceTo);
		}

		buffer.Insert(replaceFrom, reindented);
		buffer.EndTransaction();
		buffer.BreakUndoGroup();

		const std::size_t end = replaceFrom + reindented.size();
		PlaceCaret(caret, end, end);

		return true;
	}
}
