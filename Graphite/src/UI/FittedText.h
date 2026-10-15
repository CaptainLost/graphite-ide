#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	std::optional<std::size_t> CutToFit(std::string_view text, float maxWidth, float ellipsisWidth,
										const std::function<float(std::string_view)>& widthOf);

	// WARNING: Empty when the room is narrower than the ellipsis by more than the pixel CalcTextSize rounds up, because part
	// of an ellipsis says less than nothing.
	std::string FitToWidth(std::string_view text, float room, bool* cut = nullptr);

	std::string FitToLine(std::string_view text, bool* cut = nullptr);
}
