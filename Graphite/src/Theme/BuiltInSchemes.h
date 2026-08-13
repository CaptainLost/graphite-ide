#pragma once

#include "Theme/ColorScheme.h"

#include <string_view>

namespace Graphite {

	const ResolvedScheme& DarkScheme();
	const ResolvedScheme& LightScheme();

	// Null when the name is not one of the built-ins, which is how a scheme naming a base that
	// does not exist is detected rather than guessed at.
	const ResolvedScheme* BuiltInScheme(std::string_view name);

	// The names SchemeStore refuses to write, so "the built-in one cannot be edited" stays true
	// for the first person who names their file dark.json.
	bool IsBuiltInName(std::string_view name);
}
