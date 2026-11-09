#pragma once

#include "Theme/BuiltInScheme.h"
#include "Theme/ColorScheme.h"

#include <optional>
#include <span>
#include <string_view>

namespace Graphite {

	const ResolvedScheme& DarkScheme();
	const ResolvedScheme& LightScheme();

	// Every built-in scheme, the original dark and light first, in the order the settings list them.
	std::span<const BuiltInScheme> AllBuiltInSchemes();

	const ResolvedScheme& SchemeFor(BuiltInScheme scheme);

	std::string_view NameOf(BuiltInScheme scheme);

	std::optional<BuiltInScheme> ParseBuiltInName(std::string_view name);

	// How the settings show a scheme: a built-in one's stored name in words, "Gruvbox Dark Hard" for
	// "gruvbox-dark-hard"; a scheme of the user's as it was named.
	std::string SchemeDisplayName(std::string_view name);
}
