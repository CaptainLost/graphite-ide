#pragma once

#include "Lang/Api/LanguageId.h"

#include <filesystem>

namespace Graphite {

	LanguageId LanguageOfPath(const std::filesystem::path& path);
}
