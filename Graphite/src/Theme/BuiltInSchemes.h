#pragma once

#include "Theme/BuiltInScheme.h"
#include "Theme/ColorScheme.h"

#include <optional>
#include <string_view>

namespace Graphite {

	const ResolvedScheme& DarkScheme();
	const ResolvedScheme& LightScheme();

	const ResolvedScheme& Get(BuiltInScheme scheme);

	// The text a base is written as in a scheme file.
	std::string_view NameOf(BuiltInScheme scheme);

	// Nothing when the text names no built-in, which is how a scheme naming a base that does not
	// exist is detected at the point it is read rather than guessed at later.
	std::optional<BuiltInScheme> ParseBuiltInName(std::string_view name);
}
