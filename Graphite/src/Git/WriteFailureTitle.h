#pragma once

#include "Git/GitModel.h"

#include <string_view>

namespace Graphite {

	// The title of the toast that reports a failed git write: the action, named the way the
	// menu or button that started it names it. Kept out of GitModel, which hands out only the
	// kind, so the model holds no UI text.
	std::string_view WriteFailureTitle(GitModel::WriteKind kind);
}
