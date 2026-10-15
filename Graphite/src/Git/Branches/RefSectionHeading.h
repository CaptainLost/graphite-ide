#pragma once

#include <string>

namespace Graphite {

	// One text row high, like every other row of the branch list. True while expanded.
	bool DrawRefSectionHeading(const char* id, const std::string& text, bool& collapsed);
}