#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct GitActivity
	{
		std::string Text;
		std::string Tooltip;

		bool Busy = false;

		bool OpensGit = false;

		friend bool operator==(const GitActivity&, const GitActivity&) = default;
	};

	struct GitStatusSegment
	{
		std::string Branch;
		std::string BranchTooltip;

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

		std::optional<std::string> CheckingOut;

		std::optional<GitActivity> Activity;
	};

	std::string CheckingOutText(const std::string& targetLabel);

	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs);
}
