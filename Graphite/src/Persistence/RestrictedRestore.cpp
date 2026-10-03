#include "Persistence/RestrictedRestore.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		// A diff's path is relative to the repository's working directory, which is not known
		// until git has opened it and may lie above the folder. What can be judged now is that
		// the path names nothing above wherever it is joined: relative, and not climbing out.
		// Joined onto the folder for the test only because IsSameOrWithin needs a real place.
		bool StaysInside(const std::filesystem::path& relative, const std::filesystem::path& root)
		{
			return relative.is_relative() && !relative.has_root_name() && !relative.has_root_directory() &&
				   Core::IsSameOrWithin(root / relative, root);
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

		std::erase_if(stored.Diffs, [&root](const DiffTabState& diff) { return !StaysInside(diff.Path, root); });

		return stored;
	}
}
