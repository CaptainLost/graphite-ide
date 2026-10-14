#include "Solution/TextEdit.h"

namespace Graphite {

	namespace {
		constexpr const char* kDefaultIndentUnit = "  ";
		constexpr std::size_t kSelfClosingSlashDistanceFromTagEnd = 2;

		enum class TagKind
		{
			Opening,
			Closing,
		};

		bool IsLineSpace(char character)
		{
			return character == ' ' || character == '\t';
		}

		bool IsNameTerminator(char character)
		{
			return character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
				   character == '>';
		}

		bool NameEndsHere(std::string_view text, std::size_t nameEnd, TagKind tagKind)
		{
			const char following = nameEnd < text.size() ? text[nameEnd] : '\0';

			if (IsNameTerminator(following))
			{
				return true;
			}

			return tagKind == TagKind::Opening && following == '/';
		}
	}

	std::string DetectLineEnding(std::string_view text)
	{
		const std::size_t newline = text.find('\n');

		if (newline == std::string_view::npos)
		{
			return "\n";
		}

		const bool newlineFollowsCarriageReturn = newline > 0 && text[newline - 1] == '\r';

		return newlineFollowsCarriageReturn ? "\r\n" : "\n";
	}

	std::string IndentUnit(std::string_view text)
	{
		std::size_t position = 0;

		while (position < text.size())
		{
			const std::size_t newline = text.find('\n', position);

			if (newline == std::string_view::npos)
			{
				break;
			}

			const std::size_t lineStart = newline + 1;
			std::size_t indentEnd = lineStart;

			while (indentEnd < text.size() && IsLineSpace(text[indentEnd]))
			{
				++indentEnd;
			}

			const bool lineIsIndented = indentEnd > lineStart;
			const bool lineHasContent =
				indentEnd < text.size() && text[indentEnd] != '\r' && text[indentEnd] != '\n';

			if (lineIsIndented && lineHasContent)
			{
				return std::string{text.substr(lineStart, indentEnd - lineStart)};
			}

			position = lineStart;
		}

		return kDefaultIndentUnit;
	}

	std::size_t OpeningTagEnd(std::string_view text, std::size_t offset)
	{
		bool insideQuotes = false;
		char openingQuote = '\0';

		for (std::size_t position = offset; position < text.size(); ++position)
		{
			const char character = text[position];

			if (insideQuotes)
			{
				if (character == openingQuote)
				{
					insideQuotes = false;
				}

				continue;
			}

			if (character == '"' || character == '\'')
			{
				insideQuotes = true;
				openingQuote = character;
				continue;
			}

			if (character == '>')
			{
				return position + 1;
			}
		}

		return text.size();
	}

	bool EndsSelfClosingTag(std::string_view text, std::size_t tagEnd)
	{
		return tagEnd >= kSelfClosingSlashDistanceFromTagEnd &&
			   text[tagEnd - kSelfClosingSlashDistanceFromTagEnd] == '/';
	}

	std::size_t ElementEnd(std::string_view text, std::size_t offset, std::string_view name)
	{
		const std::size_t tagEnd = OpeningTagEnd(text, offset);

		if (EndsSelfClosingTag(text, tagEnd))
		{
			return tagEnd;
		}

		const std::string openingPrefix = std::string{"<"} + std::string{name};
		const std::string closingPrefix = std::string{"</"} + std::string{name};

		std::size_t nestingDepth = 1;
		std::size_t position = tagEnd;

		while (position < text.size())
		{
			const std::size_t nextOpening = text.find(openingPrefix, position);
			const std::size_t nextClosing = text.find(closingPrefix, position);

			if (nextClosing == std::string_view::npos)
			{
				return text.size();
			}

			if (nextOpening != std::string_view::npos && nextOpening < nextClosing)
			{
				const bool childIsSelfClosing =
					EndsSelfClosingTag(text, OpeningTagEnd(text, nextOpening + 1));
				const bool childHasSameName =
					NameEndsHere(text, nextOpening + openingPrefix.size(), TagKind::Opening);

				if (childHasSameName && !childIsSelfClosing)
				{
					++nestingDepth;
				}

				position = nextOpening + openingPrefix.size();
				continue;
			}

			if (!NameEndsHere(text, nextClosing + closingPrefix.size(), TagKind::Closing))
			{
				position = nextClosing + closingPrefix.size();
				continue;
			}

			--nestingDepth;
			position = OpeningTagEnd(text, nextClosing + 1);

			if (nestingDepth == 0)
			{
				return position;
			}
		}

		return text.size();
	}

	std::pair<std::size_t, std::size_t> LineExtent(std::string_view text, std::size_t position)
	{
		std::size_t lineBegin = position;

		while (lineBegin > 0 && text[lineBegin - 1] != '\n')
		{
			--lineBegin;
		}

		const std::size_t newline = text.find('\n', position);
		const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline + 1;

		return {lineBegin, lineEnd};
	}
}
