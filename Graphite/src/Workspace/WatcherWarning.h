#pragma once

#include "UI/ToastEvent.h"

#include <filesystem>

namespace Graphite {

	ToastEvent NotWatchingWarning(const std::filesystem::path& root);
}
