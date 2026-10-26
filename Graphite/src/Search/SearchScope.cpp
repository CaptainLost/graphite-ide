#include "Search/SearchScope.h"

namespace Graphite {

	SearchScope ScopeOfSearch(const SearchScopeInputs& inputs)
	{
		// WARNING: The setting outlives the solution it was set for, so a folder with none searches whole.
		if (!inputs.ProjectsOnly || !inputs.SolutionSelected)
		{
			return SearchScope::WholeFolder;
		}

		if (inputs.SolutionLoaded)
		{
			return SearchScope::Projects;
		}

		if (inputs.SolutionBeingRead)
		{
			return SearchScope::WaitForSolution;
		}

		return SearchScope::SolutionUnreadable;
	}
}
