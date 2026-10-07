#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace Graphite {

	using TextWidthMeasure = std::function<float(std::string_view text)>;

	// Cut on the left, not the right: the folder's own name is at the end of the path, and it is
	// the part that tells one folder from another.
	std::string ElideFolderLine(std::string_view folder, float width, const TextWidthMeasure& measure);
}
