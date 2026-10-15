#include "Review/ReviewFiles.h"

#include "Git/DefaultBranch.h"
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

		constexpr std::string_view kRenameArrow = " → ";

		constexpr const char* kCommittedFilesHeading = "Files";
		constexpr const char* kUncommittedHeading = "Uncommitted in worktree";

		constexpr const char* kSeparator = " \xC2\xB7 ";

		const char* HeadingOf(ReviewGroup group)
		{
			switch (group)
			{
			case ReviewGroup::Commits:
				return kCommittedFilesHeading;
			case ReviewGroup::Uncommitted:
				return kUncommittedHeading;
			}

			return kCommittedFilesHeading;
		}

		ReviewOffer OfferAgainst(const Core::Branch& branch, const Core::Branch& base)
		{
			return ReviewOffer{
				.Label = std::string{kReviewAgainst} + base.Name, .Branch = branch.FullName, .Base = base.FullName};
		}

		std::optional<ReviewOffer> OfferAgainstUpstream(const Core::Branch& branch,
														const std::vector<Core::Branch>& branches)
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

			return OfferAgainst(branch, *upstream);
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
		const Core::Branch* defaultBranch = DefaultBranch(branches);
		const Core::Branch* base = defaultBranch != nullptr ? defaultBranch : CheckedOutBranch(branches);

		if (base == nullptr)
		{
			return ReviewOffer{.Label = std::string{}, .Branch = branch.FullName, .Base = std::nullopt};
		}

		if (base->FullName == branch.FullName)
		{
			return OfferAgainstUpstream(branch, branches);
		}

		return OfferAgainst(branch, *base);
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

	std::optional<std::size_t> IndexOfCommit(const std::vector<Core::CommitSummary>& commits, const Core::Oid& id)
	{
		for (std::size_t commitIndex = 0; commitIndex < commits.size(); ++commitIndex)
		{
			if (commits[commitIndex].Id == id)
			{
				return commitIndex;
			}
		}

		return std::nullopt;
	}

	std::string CommitsTabLabel(const ReviewRange& range)
	{
		const char* truncationMark = range.CommitsComplete ? "" : "+";
		return std::format("Commits ({}{})", range.Commits.size(), truncationMark);
	}

	std::string CommitLine(const Core::CommitSummary& commit, const Core::Diff* diff)
	{
		const std::string line = commit.Id.ShortHex() + kSeparator + commit.Author.Name;

		if (diff == nullptr)
		{
			return line;
		}

		return line + kSeparator + ChangeSummary(*diff);
	}

	std::optional<Core::Oid> FollowCommit(const std::optional<Core::Oid>& selected,
										  const std::vector<Core::CommitSummary>& commits)
	{
		if (!selected.has_value() || !IndexOfCommit(commits, *selected).has_value())
		{
			return std::nullopt;
		}

		return selected;
	}

	std::optional<Core::Oid> NeighbourCommit(const std::optional<Core::Oid>& selected,
											 const std::vector<Core::CommitSummary>& commits, ListStep step)
	{
		if (commits.empty())
		{
			return std::nullopt;
		}

		const std::optional<std::size_t> picked =
			selected.has_value() ? IndexOfCommit(commits, *selected) : std::nullopt;

		if (!picked.has_value())
		{
			return commits.front().Id;
		}

		switch (step)
		{
		case ListStep::Previous:
			return commits[*picked == 0 ? 0 : *picked - 1].Id;
		case ListStep::Next:
			return commits[std::min(*picked + 1, commits.size() - 1)].Id;
		}

		return selected;
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

	bool OnlyWhitespaceChanged(const Core::FileDiff& file, Core::WhitespaceInDiff whitespace)
	{
		const bool blobsDiffer = file.OldId != file.NewId;

		return whitespace == Core::WhitespaceInDiff::Ignored && file.Change == Core::FileChange::Modified &&
			   !file.IsBinary && blobsDiffer && file.Hunks.empty();
	}
}
