#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What git is doing for the reader, has just done, or waits on them for.
	struct GitActivity
	{
		std::string Text;
		std::string Tooltip;

		// Drawn after a loader, while the work runs.
		bool Busy = false;

		// A click brings the Git panel forward, where the answer is waited for.
		bool OpensGit = false;

		friend bool operator==(const GitActivity&, const GitActivity&) = default;
	};

	struct GitStatusSegment
	{
		std::string Branch;
		std::string BranchTooltip;

		// The branch item names a checkout under way, after a loader, in place of the head it leaves.
		bool BranchBusy = false;

		std::string Distance;
		std::string DistanceTooltip;
		std::string Changes;
		std::string ChangesTooltip;
		bool HasRepository = false;

		std::optional<GitActivity> Activity;
	};

	struct GitStatusInputs
	{
		bool FolderOpen = false;
		bool MissingRepository = false;
		const Core::HeadState* Head = nullptr;
		const std::vector<Core::Branch>* Branches = nullptr;
		const Core::WorkingTreeStatus* Status = nullptr;

		// The label of a checkout under way, from its click until the head after it lands.
		std::optional<std::string> CheckingOut;

		std::optional<GitActivity> Activity;
	};

	// "Checking out feature/x…"
	std::string CheckingOutText(const std::string& targetLabel);

	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs);
}
