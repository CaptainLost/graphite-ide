#pragma once

#include <string_view>

namespace Graphite {

	enum class GitIgnoredDisplay
	{
		Show,
		Dim,
		Hide
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