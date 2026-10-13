#include "Review/ViewedMarks.h"

#include "Git/History/CommitLabels.h"

namespace Graphite {

	ViewedState StateIn(const BranchMarks& marks, ReviewGroup group, const Core::FileDiff& file)
	{
		const BranchMarks::const_iterator found = marks.find(MarkKey{.Group = group, .Path = ShownPath(file)});

		if (found == marks.end())
		{
			return ViewedState::NotViewed;
		}

		const auto& [key, mark] = *found;
		const bool sameChange = mark.Old == file.OldId && mark.New == file.NewId;
		return sameChange ? ViewedState::Viewed : ViewedState::ChangedSinceViewed;
	}
}
