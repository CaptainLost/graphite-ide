#include "Git/StashBranch.h"

#include <array>

namespace Graphite {

	std::optional<std::string> StashBranch(std::string_view message)
	{
		constexpr std::array<std::string_view, 2> prefixes{"WIP on ", "On "};

		for (const std::string_view prefix : prefixes)
		{
			if (!message.starts_with(prefix))
			{
				continue;
			}

			const std::string_view rest = message.substr(prefix.size());
			const std::size_t colon = rest.find(':');

			// A branch name has no spaces either, so one before the colon means the colon
			// belongs to a message that only happens to start like git's.
			if (colon == std::string_view::npos || colon == 0 ||
				rest.substr(0, colon).find(' ') != std::string_view::npos)
			{
				return std::nullopt;
			}

			return std::string(rest.substr(0, colon));
		}

		return std::nullopt;
	}
}
