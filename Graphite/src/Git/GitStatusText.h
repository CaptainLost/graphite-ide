#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The git part of the status bar, as three separately clickable pieces. Preformatted
	// here so StatusBar.cpp stays free of GitModel, the way ServerStatusText keeps it free
	// of the language servers.
	struct GitStatusSegment
	{
		// "<icon> main", "<icon> 7d49d3d (detached)", "<icon> main · MERGING",
		// "No repository".
		std::string Branch;
		std::string BranchTooltip;

		// "<up>2 <down>1", "<check>", or empty when there is no upstream or no measured
		// distance to it.
		std::string Distance;

		// The distance in words, "2 commits ahead of origin/main"; empty with Distance. The marks
		// alone do not say what they are measured against.
		std::string DistanceTooltip;

		// "<dot> 3 changes", or empty when the working tree is clean.
		std::string Changes;

		// What the count counts and what a click on it does; empty with Changes.
		std::string ChangesTooltip;

		// False only for "No repository": the piece is greyed and does nothing on a click.
		bool HasRepository = false;
	};

	// Pointers rather than references: each is a snapshot the model may not have yet.
	struct GitStatusInputs
	{
		bool FolderOpen = false;

		// IsMissingRepository's answer for the same frame.
		bool MissingRepository = false;

		const Core::HeadState* Head = nullptr;
		const std::vector<Core::Branch>* Branches = nullptr;
		const Core::WorkingTreeStatus* Status = nullptr;
	};

	// Nothing when no folder is open, and nothing while the head has not arrived: a bar
	// that guesses a branch and then corrects itself reads as the branch having changed.
	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs);
}
