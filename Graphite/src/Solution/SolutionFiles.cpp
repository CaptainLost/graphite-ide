#include "Solution/SolutionFiles.h"

#include "Solution/SolutionReader.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Result.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <string>

namespace Graphite {

	namespace {

		bool IsNewerFormat(const std::filesystem::path& solution)
		{
			return Core::LoweredExtension(solution) == ".slnx";
		}

		std::string LoweredStem(const std::filesystem::path& solution)
		{
			return Core::Lowered(Core::ToUtf8(solution.stem()));
		}

		bool HasNewerFormatOf(const std::vector<std::filesystem::path>& solutions, const std::filesystem::path& older)
		{
			for (const std::filesystem::path& solution : solutions)
			{
				if (IsNewerFormat(solution) && LoweredStem(solution) == LoweredStem(older))
				{
					return true;
				}
			}

			return false;
		}
	}

	std::vector<std::filesystem::path> FindSolutions(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> found;
		std::vector<std::filesystem::path> solutions;

		const Core::Result<std::vector<Core::DirectoryEntry>> entries = Core::ReadDirectory(folder);

		if (!entries)
		{
			return solutions;
		}

		for (const Core::DirectoryEntry& entry : entries.GetValue())
		{
			if (!entry.IsDirectory && IsSolutionFile(entry.Path))
			{
				found.push_back(entry.Path);
			}
		}

		for (const std::filesystem::path& solution : found)
		{
			const bool hiddenByNewerFormat = !IsNewerFormat(solution) && HasNewerFormatOf(found, solution);

			if (!hiddenByNewerFormat)
			{
				solutions.push_back(solution);
			}
		}

		std::sort(solutions.begin(), solutions.end(), ListsSolutionBefore);

		return solutions;
	}

	bool ListsSolutionBefore(const std::filesystem::path& left, const std::filesystem::path& right)
	{
		const std::string leftStem = LoweredStem(left);
		const std::string rightStem = LoweredStem(right);

		if (leftStem != rightStem)
		{
			return leftStem < rightStem;
		}

		if (IsNewerFormat(left) != IsNewerFormat(right))
		{
			return IsNewerFormat(left);
		}

		return left < right;
	}
}
