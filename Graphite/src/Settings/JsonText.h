#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace Graphite {

	// Null for empty text and for text that does not parse; it never throws.
	nlohmann::json ParseJsonText(std::string_view text);
}
