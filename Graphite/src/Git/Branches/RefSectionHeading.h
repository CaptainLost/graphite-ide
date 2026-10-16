#pragma once

#include <string>

namespace Graphite {

	// One text row high, like every other row of the branch list. True while expanded.
	// The title in the text colour and the count dim after it, as every count in a list is.
	bool DrawRefSectionHeading(const char* id, const std::string& title, const std::string& count, bool& collapsed);
}
