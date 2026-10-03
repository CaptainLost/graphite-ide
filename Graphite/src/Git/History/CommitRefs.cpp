#include "Git/History/CommitRefs.h"

#include <algorithm>

namespace Graphite {

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches)
	{
		CommitRefMap refs;

		for (const Core::Branch& branch : branches)
		{
			// A remote's HEAD is a pointer to its default branch, not a branch: the one it
			// points at is listed too, and a label for each would say the same thing twice.
			if (branch.Kind == Core::RefKind::Remote && branch.Name.ends_with("/HEAD"))
			{
				continue;
			}

			CommitRefKind kind = CommitRefKind::Local;

			if (branch.Kind == Core::RefKind::Remote)
			{
				kind = CommitRefKind::Remote;
			}
			else if (branch.IsHead)
			{
				kind = CommitRefKind::Head;
			}

			refs[branch.Tip].push_back(CommitRef{.Name = branch.Name, .Kind = kind});
		}

		for (auto& entry : refs)
		{
			std::ranges::sort(entry.second, [](const CommitRef& left, const CommitRef& right) {
				if (left.Kind != right.Kind)
				{
					return left.Kind < right.Kind;
				}

				return left.Name < right.Name;
			});
		}

		return refs;
	}

	std::size_t RefLabelsThatFit(const std::vector<float>& widths, float gap, float available,
								 float summaryWidth, float minSummary)
	{
		const float room = available - std::min(summaryWidth, minSummary) - gap;

		std::size_t count = 0;
		float used = 0.0f;

		for (const float width : widths)
		{
			const float next = used + (count == 0 ? 0.0f : gap) + width;

			if (next > room)
			{
				break;
			}

			used = next;
			++count;
		}

		return count;
	}
}
