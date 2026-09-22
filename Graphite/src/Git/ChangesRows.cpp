#include "Git/ChangesRows.h"

#include <algorithm>

namespace Graphite {

	namespace {
		bool Has(Core::StatusFlags flags, Core::StatusFlags wanted)
		{
			return (flags & wanted) == wanted;
		}
	}

	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status)
	{
		ChangeSections sections;

		for (std::size_t index = 0; index < status.Entries.size(); ++index)
		{
			const Core::FileStatus& file = status.Entries[index];

			if (file.IsConflicted)
			{
				sections.Conflicted.push_back(index);
				continue;
			}

			if (file.Index != Core::StatusFlags::None)
			{
				sections.Staged.push_back(index);
			}

			if (file.WorkTree != Core::StatusFlags::None)
			{
				sections.Unstaged.push_back(index);
			}
		}

		return sections;
	}

	char ChangeLetter(Core::StatusFlags flags, bool untracked)
	{
		// Untracked first: git shows these as ?? rather than A, and the difference matters
		// here because discarding one deletes it rather than restoring it.
		if (untracked)
		{
			return '?';
		}

		// Ordered by what a reader most needs told. A rename beats a modification because
		// the path on screen has already changed and nothing else would explain it; a
		// deletion beats both because the file is not there at all.
		if (Has(flags, Core::StatusFlags::Deleted))
		{
			return 'D';
		}

		if (Has(flags, Core::StatusFlags::Renamed))
		{
			return 'R';
		}

		if (Has(flags, Core::StatusFlags::New))
		{
			return 'A';
		}

		if (Has(flags, Core::StatusFlags::TypeChanged))
		{
			return 'T';
		}

		if (Has(flags, Core::StatusFlags::Modified))
		{
			return 'M';
		}

		return ' ';
	}

	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after)
	{
		if (std::find(after.begin(), after.end(), selected) != after.end())
		{
			return selected;
		}

		if (after.empty())
		{
			return std::nullopt;
		}

		const auto was = std::find(before.begin(), before.end(), selected);

		if (was == before.end())
		{
			return std::nullopt;
		}

		const std::size_t position = static_cast<std::size_t>(was - before.begin());

		return after[std::min(position, after.size() - 1)];
	}
}
