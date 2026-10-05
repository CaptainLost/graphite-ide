#include "corepch.h"

#include "Core/Base/Strings.h"

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
			const std::size_t segmentEnd = separatorIndex == std::string_view::npos ? text.size() : separatorIndex;

			if (segmentEnd > segmentStart)
			{
				parts.emplace_back(text.substr(segmentStart, segmentEnd - segmentStart));
			}

			if (separatorIndex == std::string_view::npos)
			{
				break;
			}

			segmentStart = separatorIndex + 1;
		}

		return parts;
	}
}
