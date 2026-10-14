#pragma once

#include "Review/ReviewPort.h"
#include "Review/ViewedMarks.h"

#include "Core/Git/Diff.h"
#include "Core/Git/Ref.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What a branch's menu offers: a review of that branch against the base it would go into.
	struct ReviewOffer
	{
		// Empty while HEAD is detached: nothing is offered then.
		std::string Label;
		std::string Branch;

		// Absent while HEAD is detached: there is no default base to review against.
		std::optional<std::string> Base;
	};

	// Against the checked-out branch for every other branch; against its upstream for the
	// checked-out branch itself, which is offered nothing without one.
	std::optional<ReviewOffer> OfferReview(const Core::Branch& branch, const std::vector<Core::Branch>& branches);

	// "feature/x" for "refs/heads/feature/x", "origin/main" for "refs/remotes/origin/main".
	std::string ShortBranchName(const std::string& fullName);

	// A commit's own change: the range from its first parent to it. A root commit - listed once the
	// branch has merged an unrelated history - has no parent, and its range starts at a zero id.
	RangeEnds EndsOfCommit(const Core::CommitSummary& commit);

	struct LineCounts
	{
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	LineCounts CountLines(const Core::FileDiff& file);

	// Each file's counts, in the diff's own order.
	std::vector<LineCounts> LineCountsOfEach(const Core::Diff& diff);

	LineCounts SumOf(const std::vector<LineCounts>& counts);

	struct ReviewGroups
	{
		const Core::Diff* Commits = nullptr;

		// Null while the toggle is off or the answer is on its way.
		const Core::Diff* Uncommitted = nullptr;

		std::array<bool, kReviewGroupCount> Collapsed{};
	};

	const Core::Diff* DiffOf(const ReviewGroups& groups, ReviewGroup group);

	// What the header says of the committed range: "2 files +2 -1".
	std::string ChangeSummary(const Core::Diff& diff);

	// The same from counts already made.
	std::string ChangeSummary(std::size_t fileCount, const LineCounts& lines);

	// "Files (40)", or "Files (12 of 40)" while a filter or Hide viewed leaves some out.
	std::string GroupHeadingText(ReviewGroup group, std::size_t shown, std::size_t total);

	struct ReviewSelection
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const ReviewSelection&, const ReviewSelection&) = default;
	};

	// Null when the selection's group is not loaded or no longer has that file.
	const Core::FileDiff* SelectedFile(const ReviewGroups& groups, const ReviewSelection& selection);

	enum class ListStep
	{
		Previous,
		Next
	};

	struct ViewedCount
	{
		std::size_t Viewed = 0;
		std::size_t Total = 0;
	};

	ViewedCount CountViewed(const ReviewGroups& groups, const ViewedStateOf& stateOf);

	// "merge-base 77d5037 · 3 commits"; a list cut at its ceiling reads "500+ commits".
	std::string RangeSummary(const ReviewRange& range);

	std::optional<std::size_t> IndexOfCommit(const std::vector<Core::CommitSummary>& commits, const Core::Oid& id);

	// "Commits (3)", or "Commits (500+)" for a list cut at its ceiling.
	std::string CommitsTabLabel(const ReviewRange& range);

	// "77d5037 · Ada · 2 files +2 -1"; without the counts while the commit's diff is on its way.
	std::string CommitLine(const Core::CommitSummary& commit, const Core::Diff* diff);

	// The picked commit while the range still lists it.
	std::optional<Core::Oid> FollowCommit(const std::optional<Core::Oid>& selected,
										  const std::vector<Core::CommitSummary>& commits);

	// One commit up or down the list, staying put at either end; the first one when none is picked.
	std::optional<Core::Oid> NeighbourCommit(const std::optional<Core::Oid>& selected,
											 const std::vector<Core::CommitSummary>& commits, ListStep step);

	std::string RangeProblemMessage(RangeProblem problem, const std::string& branchFullName,
									const std::string& baseFullName);

	std::string AlreadyMergedMessage(const std::string& branchFullName, const std::string& baseFullName);

	// A row's path in three parts, drawn in turn: where a rename came from, the directory, the name.
	struct PathLabel
	{
		std::string Origin;
		std::string Directory;
		std::string Name;
	};

	PathLabel LabelOf(const Core::FileDiff& file);

	// A file listed as modified, whose content changed, that has no lines left once white space is ignored.
	bool OnlyWhitespaceChanged(const Core::FileDiff& file, Core::WhitespaceInDiff whitespace);
}
