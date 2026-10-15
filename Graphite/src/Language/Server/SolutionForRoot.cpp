#include "Language/Server/SolutionForRoot.h"

#include "Solution/SolutionFiles.h"

#include "Core/FileSystem/FileSystem.h"

#include <vector>

namespace Graphite {

	std::optional<std::filesystem::path> SolutionForRoot(const std::filesystem::path& root, const std::filesystem::path& selected)
	{
		if (!selected.empty() && Core::IsSameOrWithin(selected, root))
		{
			return selected;
		}

		const std::vector<std::filesystem::path> solutions = FindSolutions(root);

		if (solutions.empty())
		{
			return std::nullopt;
		}

		return solutions.front();
	}
}
