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
		std::string Label;
		std::string Branch;

		// Absent while HEAD is detached: the tab asks for one.
		std::optional<std::string> Base;
	};

	// Against the checked-out branch for every other branch; against its upstream for the
	// checked-out branch itself, which is offered nothing without one.
	std::optional<ReviewOffer> OfferReview(const Core::Branch& branch, const std::vector<Core::Branch>& branches);

	std::vector<const Core::Branch*> BaseChoices(const std::vector<Core::Branch>& branches,
												 const std::string& reviewedFullName);

	// "feature/x" for "refs/heads/feature/x", "origin/main" for "refs/remotes/origin/main".
	std::string ShortBranchName(const std::string& fullName);

	// A commit's own change: the range from its first parent to it. Every commit a review lists
	// descends from the merge base, so it has a parent.
	RangeEnds EndsOfCommit(const Core::CommitSummary& commit);

	struct LineCounts
	{
		std::size_t Added = 0;
		std::size_t Removed = 0;
	};

	LineCounts CountLines(const Core::FileDiff& file);

	struct ReviewGroups
	{
		const Core::Diff* Commits = nullptr;

		// Null while the toggle is off or the answer is on its way.
		const Core::Diff* Uncommitted = nullptr;

		std::array<bool, kReviewGroupCount> Collapsed{};
	};

	const Core::Diff* DiffOf(const ReviewGroups& groups, ReviewGroup group);

	// A group's heading, or one file of that group's diff.
	struct ReviewListRow
	{
		ReviewGroup Group = ReviewGroup::Commits;
		bool IsHeading = false;

		// Into the group's diff; read only for a file.
		std::size_t FileIndex = 0;

		// How many files the group holds; read only for a heading.
		std::size_t FileCount = 0;
	};

	// Each present group's heading, then - unless it is folded - its files ordered by path.
	std::vector<ReviewListRow> BuildReviewRows(const ReviewGroups& groups);

	const Core::FileDiff& FileOfRow(const ReviewGroups& groups, const ReviewListRow& row);

	struct ReviewSelection
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const ReviewSelection&, const ReviewSelection&) = default;
	};

	// Null when the selection's group is not loaded or no longer has that file.
	const Core::FileDiff* SelectedFile(const ReviewGroups& groups, const ReviewSelection& selection);

	std::vector<std::filesystem::path> ShownPathsByName(const Core::Diff& diff);

	// The selection kept by path across a refresh, and kept while its group is still loading.
	// When its file is gone, the file that took its place in the same group; with no selection, or
	// none to take its place, the first file listed.
	std::optional<ReviewSelection> FollowSelection(const std::optional<ReviewSelection>& selected,
												   const std::vector<std::filesystem::path>& groupPathsBefore,
												   const ReviewGroups& groups, const std::optional<ReviewSelection>& firstListed);

	std::optional<ReviewSelection> FirstListedFile(const ReviewGroups& groups, const std::vector<ReviewListRow>& rows);

	enum class ListStep
	{
		Previous,
		Next
	};

	// One file up or down the list, headings skipped and groups crossed, staying put at either end.
	std::optional<ReviewSelection> NeighbourSelection(const std::optional<ReviewSelection>& selected,
													  const ReviewGroups& groups,
													  const std::vector<ReviewListRow>& rows, ListStep step);

	struct ViewedCount
	{
		std::size_t Viewed = 0;
		std::size_t Total = 0;
	};

	ViewedCount CountViewed(const ReviewGroups& groups, const ViewedMarks& marks, const std::string& branch);

	// "merge-base 77d5037 · 3 commits"; a list cut at its ceiling reads "500+ commits".
	std::string RangeSummary(const ReviewRange& range);

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
}
