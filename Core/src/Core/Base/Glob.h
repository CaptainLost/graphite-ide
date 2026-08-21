#pragma once

#include <string_view>

namespace Core {

	// One pattern, matched whole: `*` stands for any run of characters and `?` for exactly
	// one. Case is ignored, ASCII only — the same reasoning the rest of this codebase's
	// matching follows, that a locale-aware fold would promise a correctness across scripts
	// a byte comparison cannot deliver.
	//
	// Neither wildcard crosses a '/'. That is what lets a pattern say how deep it means:
	// "Core/*.cpp" names the .cpp files directly under Core and not the ones further down.
	// A caller matching a bare file name never has a separator to trip over, so the rule
	// costs it nothing.
	bool GlobMatches(std::string_view pattern, std::string_view text);
}
