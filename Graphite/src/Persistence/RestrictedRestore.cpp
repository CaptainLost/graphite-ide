#include "Persistence/RestrictedRestore.h"

#include "Core/FileSystem/FileSystem.h"

namespace Graphite {

	namespace {
		bool IsParentStep(const std::filesystem::path& element)
		{
			return element == "..";
		}

		bool HasParentStep(const std::filesystem::path& relative)
		{
			for (const std::filesystem::path& element : relative)
			{
				if (IsParentStep(element))
				{
					return true;
				}
			}

			return false;
		}

		// WARNING: A diff's path is joined onto the repository's working directory, which may lie
		// above the opened folder, so it cannot be judged against that folder. Git never reports
		// ".." in a path, so any path that has one is refused.
		bool StaysInside(const std::filesystem::path& relative)
		{
			if (!relative.is_relative() || relative.has_root_name() || relative.has_root_directory())
			{
				return false;
			}

			return !HasParentStep(relative);
		}

		bool IsTabOutside(const TabState& tab, const std::filesystem::path& root)
		{
			return !Core::IsSameOrWithin(tab.Path, root);
		}

		bool IsDiffOutside(const DiffTabState& diff)
		{
			return !StaysInside(diff.Path);
		}
	}

	WorkspaceState RestorableState(WorkspaceState stored, const std::filesystem::path& root, bool trusted)
	{
		if (trusted)
		{
			return stored;
		}

		std::erase_if(stored.Tabs, [&root](const TabState& tab) { return IsTabOutside(tab, root); });

		if (stored.Active && !Core::IsSameOrWithin(*stored.Active, root))
		{
			stored.Active.reset();
		}

		std::erase_if(stored.Diffs, IsDiffOutside);

		return stored;
	}
}
