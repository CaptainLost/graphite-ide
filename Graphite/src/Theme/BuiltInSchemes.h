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
}
