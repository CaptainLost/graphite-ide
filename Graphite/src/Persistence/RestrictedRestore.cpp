#include "Persistence/RestrictedRestore.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		// A diff's path is relative to the repository's working directory, which is not known
		// until git has opened it and may lie above the folder - so the folder cannot be what it
		// is judged against. Git never reports a path with ".." in it, so one that has any is
		// refused outright, and with it every way out of whichever directory it is joined to.
		bool StaysInside(const std::filesystem::path& relative)
		{
			if (!relative.is_relative() || relative.has_root_name() || relative.has_root_directory())
			{
				return false;
			}

			return std::none_of(relative.begin(), relative.end(),
								[](const std::filesystem::path& element) { return element == ".."; });
		}
	}

	WorkspaceState RestorableState(WorkspaceState stored, const std::filesystem::path& root, bool trusted)
	{
		if (trusted)
		{
			return stored;
		}

		std::erase_if(stored.Tabs, [&root](const TabState& tab) { return !Core::IsSameOrWithin(tab.Path, root); });

		if (stored.Active && !Core::IsSameOrWithin(*stored.Active, root))
		{
			stored.Active.reset();
		}

		std::erase_if(stored.Diffs, [](const DiffTabState& diff) { return !StaysInside(diff.Path); });

		return stored;
	}
}
