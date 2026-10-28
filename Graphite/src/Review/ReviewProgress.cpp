#include "Review/ReviewProgress.h"

#include "Git/DefaultBranch.h"

namespace Graphite {

	namespace {

		bool HasCommittedFiles(const ReviewRange& range)
		{
			return range.Ends.MergeBase != range.Ends.Tip;
		}
	}

	std::optional<std::string> ReviewBaseOf(const std::string& branch, const std::vector<Core::Branch>& branches,
											const ReviewSessions& sessions)
	{
		if (const ReviewSession* session = sessions.Find(branch); session != nullptr)
		{
			return session->Base;
		}

		if (const Core::Branch* reviewed = BranchWithFullName(branches, branch); reviewed != nullptr)
		{
			if (const std::optional<ReviewOffer> offer = OfferReview(*reviewed, branches); offer && offer->Base)
			{
				return offer->Base;
			}
		}

		const Core::Branch* trunk = TrunkBranch(branches);
		return trunk != nullptr ? std::optional<std::string>{trunk->FullName} : std::nullopt;
	}

	std::optional<ViewedCount> CountReviewProgress(const ReviewSessions& sessions, const std::string& branch,
												   const ReviewRange* range, const Core::Diff* committed)
	{
		if (range == nullptr || range->Problem != RangeProblem::None)
		{
			return std::nullopt;
		}

		if (!HasCommittedFiles(*range))
		{
			return ViewedCount{};
		}

		if (committed == nullptr)
		{
			return std::nullopt;
		}

		if (!sessions.IsKept(branch))
		{
			return ViewedCount{.Viewed = 0, .Total = committed->Files.size()};
		}

		const ReviewGroups groups{.Commits = committed};
		const ViewedStateOf stateOf = [&sessions, &branch](ReviewGroup group, const Core::FileDiff& file) {
			return sessions.StateOf(branch, group, file);
		};

		return CountViewed(groups, stateOf);
	}

	std::optional<ViewedCount> ReviewProgress::CountOf(const std::string& branch,
													   const Core::Ref<const std::vector<Core::Branch>>& branches,
													   const ReviewSessions& sessions, ReviewModel& model)
	{
		const std::optional<std::string> base = BaseOf(branch, branches, sessions);

		if (!base.has_value())
		{
			return std::nullopt;
		}

		const ReviewSession* session = sessions.Find(branch);
		const Core::WhitespaceInDiff whitespace =
			session != nullptr ? session->Settings.Whitespace : SessionSettings{}.Whitespace;
		const bool kept = sessions.IsKept(branch);

		const ReviewKey key{.Branch = branch, .Base = *base};
		model.RequestRange(key);
		const Core::Ref<const ReviewRange> range = model.GetRange(key);

		if (range == nullptr)
		{
			return std::nullopt;
		}

		// WARNING: A count without a kept session views nothing, so the marks of other sessions cannot change it.
		if (const auto held = m_Counts.find(branch); held != m_Counts.end())
		{
			const HeldCount& count = held->second;
			const bool marksUnchanged = !kept || count.SessionsVersion == sessions.GetVersion();

			if (count.Range == range && count.Whitespace == whitespace && count.Kept == kept && marksUnchanged)
			{
				return count.Count;
			}
		}

		Core::Ref<const Core::Diff> committed;

		if (range->Problem == RangeProblem::None && HasCommittedFiles(*range))
		{
			const RangeDiffKey diffKey{.Ends = range->Ends, .Whitespace = whitespace};
			model.RequestCommittedDiff(diffKey);
			committed = model.GetCommittedDiff(diffKey);
		}

		const std::optional<ViewedCount> count = CountReviewProgress(sessions, branch, range.get(), committed.get());

		if (count.has_value())
		{
			m_Counts.insert_or_assign(branch, HeldCount{.Range = range,
														.Whitespace = whitespace,
														.Kept = kept,
														.SessionsVersion = sessions.GetVersion(),
														.Count = *count});
		}

		return count;
	}

	void ReviewProgress::Clear()
	{
		m_BasesFrom.reset();
		m_Bases.clear();
		m_Counts.clear();
	}

	std::optional<std::string> ReviewProgress::BaseOf(const std::string& branch,
													  const Core::Ref<const std::vector<Core::Branch>>& branches,
													  const ReviewSessions& sessions)
	{
		if (const ReviewSession* session = sessions.Find(branch); session != nullptr)
		{
			return session->Base;
		}

		if (branches == nullptr)
		{
			return std::nullopt;
		}

		// WARNING: Working out an offered base walks every branch, so the bases are held for the snapshot they came from.
		if (m_BasesFrom != branches)
		{
			m_BasesFrom = branches;
			m_Bases.clear();
		}

		const auto [held, added] = m_Bases.try_emplace(branch);

		if (added)
		{
			held->second = ReviewBaseOf(branch, *branches, sessions);
		}

		return held->second;
	}
}
