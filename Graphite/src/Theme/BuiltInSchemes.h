#pragma once

#include "Theme/BuiltInScheme.h"
#include "Theme/ColorScheme.h"

#include <optional>
#include <string_view>

namespace Graphite {

	const ResolvedScheme& DarkScheme();
	const ResolvedScheme& LightScheme();

	const ResolvedScheme& SchemeFor(BuiltInScheme scheme);

	std::string_view NameOf(BuiltInScheme scheme);

	std::optional<BuiltInScheme> ParseBuiltInName(std::string_view name);
}
