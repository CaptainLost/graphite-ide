#pragma once

#include "Git/GitModel.h"

#include <string_view>

namespace Graphite {

	std::string_view WriteFailureTitle(GitModel::WriteKind kind);
}
