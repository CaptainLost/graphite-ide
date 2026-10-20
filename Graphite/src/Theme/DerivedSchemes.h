#pragma once

#include "Theme/BuiltInScheme.h"
#include "Theme/ColorScheme.h"

namespace Graphite {

	// A built-in scheme other than the original dark and light: a short palette of surfaces, text and syntax
	// colours, turned into every slot over the dark or light scheme it resembles.
	ResolvedScheme BuildDerivedScheme(BuiltInScheme which);
}
