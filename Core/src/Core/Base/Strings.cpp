#include "corepch.h"

#include "Core/Base/Strings.h"

#include "Core/Base/Ascii.h"

namespace Core {

	std::string JoinedWith(std::span<const std::string> parts, std::string_view separator)
	{
		std::string joined;

		for (std::size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
		{
			if (partIndex > 0)
			{
				joined += separator;
			}

			joined += parts[partIndex];
		}

		return joined;
	}

	void AppendSeparated(std::string& text, std::string_view separator, std::string_view part)
	{
		if (!text.empty())
		{
			text += separator;
		}

		text += part;
	}

	void AppendSeparatedIfNotEmpty(std::string& text, std::string_view separator, std::string_view part)
	{
		if (part.empty())
		{
			return;
		}

		AppendSeparated(text, separator, part);
	}

	std::vector<std::string> SplitSkippingEmpty(std::string_view text, char separator)
	{
		std::vector<std::string> parts;
		std::size_t segmentStart = 0;

		while (segmentStart < text.size())
		{
			const std::size_t separatorIndex = text.find(separator, segmentStart);
			const bool isLastSegment = separatorIndex == std::string_view::npos;
			const std::size_t segmentEnd = isLastSegment ? text.size() : separatorIndex;

			if (segmentEnd > segmentStart)
			{
				parts.emplace_back(text.substr(segmentStart, segmentEnd - segmentStart));
			}

			if (isLastSegment)
			{
				break;
			}

			segmentStart = separatorIndex + 1;
		}

		return parts;
	}

	std::vector<std::string> LoweredWords(std::string_view query)
	{
		constexpr char kWordSeparator = ' ';

		return SplitSkippingEmpty(Lowered(query), kWordSeparator);
	}

	std::vector<std::string_view> SplitLines(std::string_view text)
	{
		std::vector<std::string_view> lines;
		std::size_t lineStart = 0;

		while (true)
		{
			const std::size_t lineEnd = text.find('\n', lineStart);
			const bool isLastLine = lineEnd == std::string_view::npos;
			std::string_view line = text.substr(lineStart, isLastLine ? std::string_view::npos : lineEnd - lineStart);

			if (line.ends_with('\r'))
			{
				line.remove_suffix(1);
			}

			lines.push_back(line);

			if (isLastLine)
			{
				return lines;
			}

			lineStart = lineEnd + 1;
		}
	}

	std::string Counted(long long count, std::string_view singular, std::string_view plural)
	{
		std::string text = std::to_string(count);
		text.push_back(' ');
		text += count == 1 ? singular : plural;
		return text;
	}

	std::string Counted(long long count, std::string_view singular)
	{
		return Counted(count, singular, std::string{singular} + 's');
	}
}
