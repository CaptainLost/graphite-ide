#pragma once

#include "Core/Git/Head.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct GitStatusSegment
	{
		std::string Branch;
		std::string BranchTooltip;
		std::string Distance;
		std::string DistanceTooltip;
		std::string Changes;
		std::string ChangesTooltip;
		bool HasRepository = false;
	};

	struct GitStatusInputs
	{
		bool FolderOpen = false;
		bool MissingRepository = false;
		const Core::HeadState* Head = nullptr;
		const std::vector<Core::Branch>* Branches = nullptr;
		const Core::WorkingTreeStatus* Status = nullptr;
	};

	std::optional<GitStatusSegment> GitStatusText(const GitStatusInputs& inputs);
}
