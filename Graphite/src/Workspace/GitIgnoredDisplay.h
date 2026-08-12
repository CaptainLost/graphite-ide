#pragma once

#include <string_view>

namespace Graphite {

	enum class GitIgnoredDisplay
	{
		Show, ///< Draw them like any other entry.
		Dim,  ///< Draw them in the disabled colour — the default.
		Hide  ///< Leave them out of the tree.
	};

	constexpr std::string_view GitIgnoredDisplayToString(GitIgnoredDisplay display)
	{
		switch (display)
		{
		case GitIgnoredDisplay::Show:
			return "show";
		case GitIgnoredDisplay::Hide:
			return "hide";
		case GitIgnoredDisplay::Dim:
			break;
		}

		return "dim";
	}

	constexpr GitIgnoredDisplay GitIgnoredDisplayFromString(std::string_view text)
	{
		if (text == "show")
		{
			return GitIgnoredDisplay::Show;
		}

		if (text == "hide")
		{
			return GitIgnoredDisplay::Hide;
		}

		return GitIgnoredDisplay::Dim;
	}
}