#pragma once

#include "Git/History/CommitDetails.h"
#include "Theme/GitPalette.h"

#include "Core/Git/Commit.h"

#include <chrono>
#include <optional>

namespace Graphite {

	// WARNING: stats absent means the diff has not arrived, or failed; its line still draws dimmed so the pane does not change height when it lands.
	bool RenderCommitDetails(const Core::CommitSummary& commit,
							 const std::optional<DiffStats>& stats, bool diffFailed, const std::chrono::time_zone* zone,
							 const GitPalette& palette);
}
