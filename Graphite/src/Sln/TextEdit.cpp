#include "Sln/TextEdit.h"

namespace Graphite {

	std::string DetectLineEnding(std::string_view text)
	{
		const std::size_t newline = text.find('\n');

		if (newline == std::string_view::npos)
			return "\n";

		return newline > 0 && text[newline - 1] == '\r' ? "\r\n" : "\n";
	}

	std::string IndentUnit(std::string_view text)
	{
		std::size_t position = 0;

		while (position < text.size())
		{
			const std::size_t newline = text.find('\n', position);

			if (newline == std::string_view::npos)
				break;

			std::size_t run = newline + 1;

			while (run < text.size() && (text[run] == ' ' || text[run] == '\t'))
				++run;

			// A line that is only whitespace teaches nothing about indentation.
			if (run > newline + 1 && run < text.size() && text[run] != '\r' && text[run] != '\n')
				return std::string{text.substr(newline + 1, run - newline - 1)};

			position = newline + 1;
		}

		return "  ";
	}

	std::size_t OpeningTagEnd(std::string_view text, std::size_t offset)
	{
		bool inQuotes = false;
		char quote = '\0';

		for (std::size_t i = offset; i < text.size(); ++i)
		{
			const char character = text[i];

			if (inQuotes)
			{
				if (character == quote)
					inQuotes = false;

				continue;
			}

			if (character == '"' || character == '\'')
			{
				inQuotes = true;
				quote = character;
				continue;
			}

			if (character == '>')
				return i + 1;
		}

		return text.size();
	}

	bool EndsSelfClosingTag(std::string_view text, std::size_t tagEnd)
	{
		return tagEnd >= 2 && text[tagEnd - 2] == '/';
	}

	namespace {
		bool NameEndsHere(std::string_view text, std::size_t after, bool opening)
		{
			const char following = after < text.size() ? text[after] : '\0';

			if (following == ' ' || following == '\t' || following == '\r' || following == '\n' || following == '>')
			{
				return true;
			}

			// Only an opening tag may be followed by the slash of "/>".
			return opening && following == '/';
		}
	}

	std::size_t ElementEnd(std::string_view text, std::size_t offset, std::string_view name)
	{
		const std::size_t tagEnd = OpeningTagEnd(text, offset);

		if (EndsSelfClosingTag(text, tagEnd))
			return tagEnd;

		// Nesting is counted so a child of the same name does not end the parent early.
		const std::string open = std::string{"<"} + std::string{name};
		const std::string close = std::string{"</"} + std::string{name};

		std::size_t depth = 1;
		std::size_t position = tagEnd;

		while (position < text.size())
		{
			const std::size_t nextOpen = text.find(open, position);
			const std::size_t nextClose = text.find(close, position);

			if (nextClose == std::string_view::npos)
				return text.size();

			if (nextOpen != std::string_view::npos && nextOpen < nextClose)
			{
				// A self-closing child has no closing tag of its own, so counting it would
				// leave the depth one too high and swallow the rest of the file.
				const bool selfClosing =
					EndsSelfClosingTag(text, OpeningTagEnd(text, nextOpen + 1));

				if (NameEndsHere(text, nextOpen + open.size(), true) && !selfClosing)
					++depth;

				position = nextOpen + open.size();
				continue;
			}

			if (!NameEndsHere(text, nextClose + close.size(), false))
			{
				position = nextClose + close.size();
				continue;
			}

			--depth;
			position = OpeningTagEnd(text, nextClose + 1);

			if (depth == 0)
				return position;
		}

		return text.size();
	}

	std::pair<std::size_t, std::size_t> LineExtent(std::string_view text, std::size_t position)
	{
		std::size_t begin = position;

		while (begin > 0 && text[begin - 1] != '\n')
			--begin;

		const std::size_t newline = text.find('\n', position);
		const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;

		return {begin, end};
	}
}