#pragma once

#include <nlohmann/json.hpp>

#include <string_view>

namespace Graphite {

	nlohmann::json ParseJsonText(std::string_view text);
}
