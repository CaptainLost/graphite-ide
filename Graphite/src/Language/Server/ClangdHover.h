#pragma once

#include "Language/Api/HoverInfo.h"

#include <string_view>

namespace Graphite {

	HoverInfo ParseClangdHover(std::string_view markdown);
}
