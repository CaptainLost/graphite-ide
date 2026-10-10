#include "Language/Server/SolutionForRoot.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

namespace Graphite {

	namespace {

		bool IsSolutionFile(const std::filesystem::directory_entry& entry)
		{
			std::error_code kindError;
			const std::string extension = Core::LoweredExtension(entry.path());

			return entry.is_regular_file(kindError) && (extension == ".sln" || extension == ".slnx");
		}

		bool ComesFirstByName(const std::filesystem::path& left, const std::filesystem::path& right)
		{
			return Core::Lowered(Core::ToUtf8(left.filename())) < Core::Lowered(Core::ToUtf8(right.filename()));
		}
	}

	std::optional<std::filesystem::path> SolutionForRoot(const std::filesystem::path& root, const std::filesystem::path& selected)
	{
		if (!selected.empty() && Core::IsSameOrWithin(selected, root))
		{
			return selected;
		}

		std::vector<std::filesystem::path> solutions;
		std::error_code listError;
		std::filesystem::directory_iterator entries{root, listError};

		for (; !listError && entries != std::filesystem::directory_iterator{}; entries.increment(listError))
		{
			if (IsSolutionFile(*entries))
			{
				solutions.push_back(entries->path());
			}
		}

		if (solutions.empty())
		{
			return std::nullopt;
		}

		return *std::min_element(solutions.begin(), solutions.end(), ComesFirstByName);
	}
}
