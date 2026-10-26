#pragma once

#include <cstdint>

namespace Graphite {

	enum class SearchScope : std::uint8_t
	{
		WholeFolder,
		Projects,
		WaitForSolution,
		SolutionUnreadable
	};

	struct SearchScopeInputs
	{
		bool ProjectsOnly = false;
		bool SolutionSelected = false;
		bool SolutionLoaded = false;
		bool SolutionBeingRead = false;
	};

	SearchScope ScopeOfSearch(const SearchScopeInputs& inputs);
}
