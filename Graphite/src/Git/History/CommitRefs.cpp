#include "Git/History/CommitRefs.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool IsRemoteSymbolicHead(const Core::Branch& branch)
		{
			return branch.Kind == Core::RefKind::Remote && branch.Name.ends_with("/HEAD");
		}

		bool ListsBefore(const CommitRef& left, const CommitRef& right)
		{
			if (left.Kind != right.Kind)
			{
				return left.Kind < right.Kind;
			}

			return left.Name < right.Name;
		}
	}

	CommitRefMap BuildCommitRefs(const std::vector<Core::Branch>& branches)
	{
		CommitRefMap refs;

		for (const Core::Branch& branch : branches)
		{
			if (IsRemoteSymbolicHead(branch))
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

		for (auto& [tip, commitRefs] : refs)
		{
			std::ranges::sort(commitRefs, ListsBefore);
		}

		return refs;
	}

	std::size_t RefLabelsThatFit(const std::vector<float>& labelWidths, float gap,
								 float availableWidth, float summaryWidth,
								 float minimumSummaryWidth)
	{
		const float room = availableWidth - std::min(summaryWidth, minimumSummaryWidth) - gap;

		std::size_t fittingCount = 0;
		float usedWidth = 0.0f;

		for (const float labelWidth : labelWidths)
		{
			const float gapBefore = fittingCount == 0 ? 0.0f : gap;
			const float widthWithThisLabel = usedWidth + gapBefore + labelWidth;

			if (widthWithThisLabel > room)
			{
				break;
			}

			usedWidth = widthWithThisLabel;
			++fittingCount;
		}

		return fittingCount;
	}
}
