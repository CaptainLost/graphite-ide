#include "Review/ReviewFiles.h"

#include "Git/Changes/ChangesRows.h"
#include "Git/History/CommitLabels.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kLocalPrefix = "refs/heads/";
		constexpr std::string_view kRemotePrefix = "refs/remotes/";

		constexpr std::string_view kReviewAgainst = "Review against ";
		constexpr std::string_view kReviewChoosingBase = "Review…";

		constexpr std::string_view kRenameArrow = " → ";

		constexpr const char* kCommitsHeading = "Commits";
		constexpr const char* kUncommittedHeading = "Uncommitted in worktree";

		const char* HeadingOf(ReviewGroup group)
		{
			switch (group)
			{
			case ReviewGroup::Commits:
				return kCommitsHeading;
			case ReviewGroup::Uncommitted:
				return kUncommittedHeading;
			}

			return kCommitsHeading;
		}

		const Core::Branch* CheckedOutBranch(const std::vector<Core::Branch>& branches)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind == Core::RefKind::Local && branch.IsHead)
				{
					return &branch;
				}
			}

			return nullptr;
		}

		const Core::Branch* RemoteBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind == Core::RefKind::Remote && branch.Name == name)
				{
					return &branch;
				}
			}

			return nullptr;
		}

		bool IsShownBefore(const Core::Diff& diff, std::size_t leftIndex, std::size_t rightIndex)
		{
			return ShownPath(diff.Files[leftIndex]) < ShownPath(diff.Files[rightIndex]);
		}

		std::vector<std::size_t> FileIndicesByPath(const Core::Diff& diff)
		{
			std::vector<std::size_t> indices;
			indices.reserve(diff.Files.size());

			for (std::size_t fileIndex = 0; fileIndex < diff.Files.size(); ++fileIndex)
			{
				indices.push_back(fileIndex);
			}

			std::sort(indices.begin(), indices.end(),
					  [&diff](std::size_t left, std::size_t right) { return IsShownBefore(diff, left, right); });

			return indices;
		}

		void AddViewed(ViewedCount& count, const Core::Diff* diff, ReviewGroup group, const ViewedStateOf& stateOf)
		{
			if (diff == nullptr)
			{
				return;
			}

			for (const Core::FileDiff& file : diff->Files)
			{
				++count.Total;

				if (stateOf(group, file) == ViewedState::Viewed)
				{
					++count.Viewed;
				}
			}
		}
	}

	std::optional<ReviewOffer> OfferReview(const Core::Branch& branch, const std::vector<Core::Branch>& branches)
	{
		if (branch.IsHead)
		{
			if (!branch.Upstream.has_value())
			{
				return std::nullopt;
			}

			const Core::Branch* upstream = RemoteBranchNamed(branches, *branch.Upstream);

			if (upstream == nullptr)
			{
				return std::nullopt;
			}

			return ReviewOffer{.Label = std::string{kReviewAgainst} + upstream->Name,
							   .Branch = branch.FullName,
							   .Base = upstream->FullName};
		}

		const Core::Branch* checkedOut = CheckedOutBranch(branches);

		if (checkedOut == nullptr)
		{
			return ReviewOffer{.Label = std::string{kReviewChoosingBase}, .Branch = branch.FullName, .Base = std::nullopt};
		}

		return ReviewOffer{.Label = std::string{kReviewAgainst} + checkedOut->Name,
						   .Branch = branch.FullName,
						   .Base = checkedOut->FullName};
	}

	std::string ShortBranchName(const std::string& fullName)
	{
		if (fullName.starts_with(kLocalPrefix))
		{
			return fullName.substr(kLocalPrefix.size());
		}

		if (fullName.starts_with(kRemotePrefix))
		{
			return fullName.substr(kRemotePrefix.size());
		}

		return fullName;
	}

	RangeEnds EndsOfCommit(const Core::CommitSummary& commit)
	{
		const Core::Oid noParent;
		const Core::Oid firstParent = commit.Parents.empty() ? noParent : commit.Parents.front();
		return RangeEnds{.MergeBase = firstParent, .Tip = commit.Id};
	}

	LineCounts CountLines(const Core::FileDiff& file)
	{
		LineCounts counts;

		for (const Core::Hunk& hunk : file.Hunks)
		{
			for (const Core::DiffLine& line : hunk.Lines)
			{
				if (line.Kind == Core::DiffLineKind::Added)
				{
					++counts.Added;
				}

				if (line.Kind == Core::DiffLineKind::Removed)
				{
					++counts.Removed;
				}
			}
		}

		return counts;
	}

	std::vector<LineCounts> LineCountsOfEach(const Core::Diff& diff)
	{
		std::vector<LineCounts> counts;
		counts.reserve(diff.Files.size());

		for (const Core::FileDiff& file : diff.Files)
		{
			counts.push_back(CountLines(file));
		}

		return counts;
	}

	LineCounts SumOf(const std::vector<LineCounts>& counts)
	{
		LineCounts total;

		for (const LineCounts& fileCounts : counts)
		{
			total.Added += fileCounts.Added;
			total.Removed += fileCounts.Removed;
		}

		return total;
	}

	const Core::Diff* DiffOf(const ReviewGroups& groups, ReviewGroup group)
	{
		switch (group)
		{
		case ReviewGroup::Commits:
			return groups.Commits;
		case ReviewGroup::Uncommitted:
			return groups.Uncommitted;
		}

		return nullptr;
	}

	std::string ChangeSummary(const Core::Diff& diff)
	{
		return ChangeSummary(diff.Files.size(), SumOf(LineCountsOfEach(diff)));
	}

	std::string ChangeSummary(std::size_t fileCount, const LineCounts& lines)
	{
		const char* noun = fileCount == 1 ? "file" : "files";
		return std::format("{} {} +{} -{}", fileCount, noun, lines.Added, lines.Removed);
	}

	std::string GroupHeadingText(ReviewGroup group, std::size_t shown, std::size_t total)
	{
		if (shown == total)
		{
			return std::format("{} ({})", HeadingOf(group), total);
		}

		return std::format("{} ({} of {})", HeadingOf(group), shown, total);
	}

	const Core::FileDiff* SelectedFile(const ReviewGroups& groups, const ReviewSelection& selection)
	{
		const Core::Diff* diff = DiffOf(groups, selection.Group);

		if (diff == nullptr)
		{
			return nullptr;
		}

		for (const Core::FileDiff& file : diff->Files)
		{
			if (ShownPath(file) == selection.Path)
			{
				return &file;
			}
		}

		return nullptr;
	}

	std::vector<std::filesystem::path> ShownPathsByName(const Core::Diff& diff)
	{
		std::vector<std::filesystem::path> paths;

		for (const std::size_t fileIndex : FileIndicesByPath(diff))
		{
			paths.push_back(ShownPath(diff.Files[fileIndex]));
		}

		return paths;
	}

	std::optional<ReviewSelection> FollowSelection(const std::optional<ReviewSelection>& selected,
												   const std::vector<std::filesystem::path>& groupPathsBefore,
												   const ReviewGroups& groups, const std::optional<ReviewSelection>& firstListed)
	{
		if (selected.has_value())
		{
			const Core::Diff* diff = DiffOf(groups, selected->Group);

			if (diff == nullptr || SelectedFile(groups, *selected) != nullptr)
			{
				return selected;
			}

			const std::optional<std::filesystem::path> neighbour =
				NextSelection(selected->Path, groupPathsBefore, ShownPathsByName(*diff));

			if (neighbour.has_value())
			{
				return ReviewSelection{.Group = selected->Group, .Path = *neighbour};
			}
		}

		return firstListed;
	}

	ViewedCount CountViewed(const ReviewGroups& groups, const ViewedStateOf& stateOf)
	{
		ViewedCount count;
		AddViewed(count, groups.Commits, ReviewGroup::Commits, stateOf);
		AddViewed(count, groups.Uncommitted, ReviewGroup::Uncommitted, stateOf);
		return count;
	}

	std::string RangeSummary(const ReviewRange& range)
	{
		const std::size_t commitCount = range.Commits.size();
		const std::string countText =
			range.CommitsComplete ? std::to_string(commitCount) : std::to_string(commitCount) + "+";
		const bool exactlyOne = commitCount == 1 && range.CommitsComplete;

		return std::format("merge-base {} · {} {}", range.Ends.MergeBase.ShortHex(), countText,
						   exactlyOne ? "commit" : "commits");
	}

	std::string RangeProblemMessage(RangeProblem problem, const std::string& branchFullName,
									const std::string& baseFullName)
	{
		const std::string branch = ShortBranchName(branchFullName);
		const std::string base = ShortBranchName(baseFullName);

		switch (problem)
		{
		case RangeProblem::None:
			return std::string{};
		case RangeProblem::BranchMissing:
			return std::format("{} no longer exists.", branch);
		case RangeProblem::BaseMissing:
			return std::format("{} no longer exists.", base);
		case RangeProblem::NoCommonAncestor:
			return std::format("{} has no common ancestor with {}.", branch, base);
		}

		return std::string{};
	}

	std::string AlreadyMergedMessage(const std::string& branchFullName, const std::string& baseFullName)
	{
		return std::format("Every commit of {} is already in {}.", ShortBranchName(branchFullName),
						   ShortBranchName(baseFullName));
	}

	PathLabel LabelOf(const Core::FileDiff& file)
	{
		const std::filesystem::path& shown = ShownPath(file);

		PathLabel label;
		label.Name = Core::ToUtf8(shown.filename());

		if (shown.has_parent_path())
		{
			label.Directory = Core::ToGenericUtf8(shown.parent_path()) + "/";
		}

		const bool cameFromAnotherPath = (file.Change == Core::FileChange::Renamed ||
										  file.Change == Core::FileChange::Copied) &&
										 !file.OldPath.empty() && file.OldPath != file.NewPath;

		if (cameFromAnotherPath)
		{
			label.Origin = Core::ToGenericUtf8(file.OldPath) + std::string{kRenameArrow};
		}

		return label;
	}
}
