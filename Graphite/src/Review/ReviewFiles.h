#pragma once

#include "Git/Lines/BranchLines.h"
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

	struct ReviewOffer
	{
		std::string Branch;

		std::optional<std::string> Base;
	};

	// The base a review of the branch is offered against: its line, and for a base its upstream. A branch compared with
	// nothing is offered with no base. No offer while `lines` is null, until the first line read lands.
	std::optional<ReviewOffer> OfferReview(const Core::Branch& branch, const std::vector<Core::Branch>& branches,
										   const BranchLines* lines);


	RangeEnds EndsOfCommit(const Core::CommitSummary& commit);

	std::vector<Core::LineCounts> LineCountsOfEach(const Core::Diff& diff);

	Core::LineCounts SumOf(const std::vector<Core::LineCounts>& counts);

	struct ReviewGroups
	{
		const Core::Diff* Commits = nullptr;

		const Core::Diff* Uncommitted = nullptr;

		std::array<bool, kReviewGroupCount> Collapsed{};
	};

	const Core::Diff* DiffOf(const ReviewGroups& groups, ReviewGroup group);

	std::string ChangeSummary(const Core::Diff& diff);

	std::string ChangeSummary(std::size_t fileCount, const Core::LineCounts& lines);

	const char* GroupName(ReviewGroup group);

	std::string GroupHeadingText(ReviewGroup group, std::size_t shown, std::size_t total);

	struct ReviewRefresh
	{
		bool ShowsEarlierChanges = false;
		bool RangeLoading = false;
		bool UncommittedLoading = false;
		bool UncommittedShown = false;
		bool UncommittedFailed = false;
		bool UncommittedRetrying = false;
	};

	bool IsUpdatingChanges(const ReviewRefresh& refresh);

	bool IsUncommittedAwaited(const ReviewRefresh& refresh);

	bool ShowsUncommittedFailure(const ReviewRefresh& refresh);

	enum class DiffPaneWithoutFile
	{
		SelectAFile,
		NothingToReview,
		LoadingUncommitted
	};

	DiffPaneWithoutFile DiffPaneWithoutFileFor(std::size_t reviewFileCount, bool uncommittedLoading);

	struct ReviewSelection
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const ReviewSelection&, const ReviewSelection&) = default;
	};

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

	std::string RangeSummary(const ReviewRange& range);

	std::optional<std::size_t> IndexOfCommit(const std::vector<Core::CommitSummary>& commits, const Core::Oid& id);

	std::string CommitsTabLabel(const ReviewRange& range);

	std::string CommitLine(const Core::CommitSummary& commit, const Core::Diff* diff);

	std::optional<Core::Oid> FollowCommit(const std::optional<Core::Oid>& selected,
										  const std::vector<Core::CommitSummary>& commits);

	std::optional<Core::Oid> NeighbourCommit(const std::optional<Core::Oid>& selected,
											 const std::vector<Core::CommitSummary>& commits, ListStep step);

	std::string RangeProblemMessage(RangeProblem problem, const std::string& branchFullName,
									const std::string& baseFullName);

	std::string AlreadyMergedMessage(const std::string& branchFullName, const std::string& baseFullName);

	struct PathLabel
	{
		std::string Origin;
		std::string Directory;
		std::string Name;
	};

	PathLabel LabelOf(const Core::FileDiff& file);

	bool OnlyWhitespaceChanged(const Core::FileDiff& file, Core::WhitespaceInDiff whitespace);
}
