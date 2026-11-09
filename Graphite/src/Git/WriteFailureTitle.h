#pragma once

#include "Git/GitModel.h"

#include <string>
#include <string_view>

namespace Graphite {

	std::string_view WriteFailureTitle(GitModel::WriteKind kind);

	// A write into another worktree: "Commit failed in <folder>".
	std::string WorktreeWriteFailureTitle(GitModel::WriteKind kind, std::string_view folder);
}
