#pragma once

#include "Core/Search/Matcher.h"
#include "Search/SearchQuery.h"

// The matcher lives in Core, where the WSL agent searches with it too.
namespace Graphite {

	using Core::FindInLine;
	using Core::FindInText;
	using Core::LineMatch;
	using Core::MaskAccepts;
}
