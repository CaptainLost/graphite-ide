#pragma once

#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"

#include <string_view>

namespace Graphite {

	// Which of the lines items a branch's menu offers.
	struct BranchMenu
	{
		bool UseAsBase = false;
		bool StopUsingAsBase = false;
		bool UseAsMainLine = false;
		bool CompareWith = false;
	};

	// Nothing is offered without a read, or for a name the read does not list.
	BranchMenu MenuFor(const LineRead* read, const BranchLines* lines, std::string_view fullName);
}
