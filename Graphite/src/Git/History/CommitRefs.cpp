#include "Git/History/CommitRefs.h"

#include <algorithm>

namespace Graphite {

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches)
	{
		CommitRefMap refs;

		for (const Core::Branch& branch : branches)
		{
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

	std::size_t RefLabelsThatFit(const std::vector<float>& widths, float gap, float available)
	{
		std::size_t count = 0;
		float used = 0.0f;

		for (const float width : widths)
		{
			const float next = used + (count == 0 ? 0.0f : gap) + width;

			if (next > available)
			{
				break;
			}

			used = next;
			++count;
		}

		return count;
	}
}
