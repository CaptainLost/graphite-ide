#include "Git/Content/ContentOrder.h"

#include <algorithm>
#include <chrono>

namespace Graphite {

	namespace {

		// Declared in the order the groups are checked.
		enum class CheckGroup
		{
			Shown,
			InView,
			Yours,
			Remote,
		};

		struct RankedCandidate
		{
			std::size_t Branch = 0;
			CheckGroup Group = CheckGroup::Yours;
			std::optional<std::chrono::sys_seconds> TipTime;
		};

		bool IsCheckedBefore(const RankedCandidate& left, const RankedCandidate& right)
		{
			if (left.Group != right.Group)
			{
				return left.Group < right.Group;
			}

			if (left.TipTime.has_value() != right.TipTime.has_value())
			{
				return left.TipTime.has_value();
			}

			return left.TipTime.has_value() && *left.TipTime > *right.TipTime;
		}

		std::optional<std::chrono::sys_seconds> TipTimeOf(const LineRead& read, std::size_t ref)
		{
			if (read.Branches == nullptr || ref >= read.Branches->size())
			{
				return std::nullopt;
			}

			const Core::Branch& branch = (*read.Branches)[ref];

			if (!branch.TipCommit.has_value())
			{
				return std::nullopt;
			}

			return branch.TipCommit->Committer.When;
		}

		CheckGroup GroupOf(const LineRead& read, std::size_t branch, const std::set<std::size_t>& inView,
						   std::optional<std::size_t> shown)
		{
			if (shown == branch)
			{
				return CheckGroup::Shown;
			}

			if (inView.contains(branch))
			{
				return CheckGroup::InView;
			}

			const Core::BranchLine& line = read.Model.Branches[branch];
			return read.Refs[line.Ref].Kind == Core::RefKind::Local ? CheckGroup::Yours : CheckGroup::Remote;
		}
	}

	bool IsContentCandidate(const Core::BranchLine& line)
	{
		return line.State == Core::BranchState::NotMerged && line.ComparedWith.has_value() && line.Ahead > 0;
	}

	std::optional<ContentKey> ContentKeyOf(const LineRead& read, const Core::BranchLine& line)
	{
		if (!line.ComparedWith.has_value())
		{
			return std::nullopt;
		}

		const std::size_t refCount = read.Refs.size();

		if (line.Ref >= refCount || *line.ComparedWith >= refCount)
		{
			return std::nullopt;
		}

		return ContentKey{read.Refs[line.Ref].Tip, read.Refs[*line.ComparedWith].Tip};
	}

	std::vector<std::size_t> OrderCandidates(const LineRead& read, const std::set<std::size_t>& inView,
											 std::optional<std::size_t> shown)
	{
		std::vector<RankedCandidate> ranked;

		for (std::size_t branch = 0; branch < read.Model.Branches.size(); ++branch)
		{
			const Core::BranchLine& line = read.Model.Branches[branch];

			if (!IsContentCandidate(line))
			{
				continue;
			}

			ranked.push_back(RankedCandidate{.Branch = branch,
											 .Group = GroupOf(read, branch, inView, shown),
											 .TipTime = TipTimeOf(read, line.Ref)});
		}

		std::stable_sort(ranked.begin(), ranked.end(), IsCheckedBefore);

		std::vector<std::size_t> order;
		order.reserve(ranked.size());

		for (const RankedCandidate& candidate : ranked)
		{
			order.push_back(candidate.Branch);
		}

		return order;
	}
}
