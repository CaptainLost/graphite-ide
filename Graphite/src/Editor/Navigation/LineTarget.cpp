#include "Editor/Navigation/LineTarget.h"

#include <charconv>

namespace Graphite {

	namespace {

		std::string_view Trimmed(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}

			while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
			{
				text.remove_suffix(1);
			}

			return text;
		}

		std::optional<std::size_t> CountingFromOne(std::string_view text)
		{
			text = Trimmed(text);
			std::size_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

			if (error != std::errc{} || end != text.data() + text.size() || value == 0)
			{
				return std::nullopt;
			}

			return value - 1;
		}
	}

	std::optional<LineTarget> ParseLineTarget(std::string_view typed)
	{
		const std::size_t colon = typed.find(':');
		const std::optional<std::size_t> line = CountingFromOne(typed.substr(0, colon));

		if (!line)
		{
			return std::nullopt;
		}

		if (colon == std::string_view::npos || Trimmed(typed.substr(colon + 1)).empty())
		{
			return LineTarget{.Line = *line};
		}

		const std::optional<std::size_t> column = CountingFromOne(typed.substr(colon + 1));

		if (!column)
		{
			return std::nullopt;
		}

		return LineTarget{.Line = *line, .Column = *column};
	}
}
