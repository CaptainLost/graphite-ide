#pragma once

#include "Git/Diff/FileTexts.h"

#include "Core/Base/Result.h"
#include "Core/Git/Commit.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// One review: a branch against the branch it would go into, both by full name.
	struct ReviewKey
	{
		std::string Branch;
		std::string Base;

		friend bool operator==(const ReviewKey&, const ReviewKey&) = default;
	};

	struct ReviewKeyHash
	{
		std::size_t operator()(const ReviewKey& key) const noexcept
		{
			const std::size_t branchHash = std::hash<std::string>{}(key.Branch);
			const std::size_t baseHash = std::hash<std::string>{}(key.Base);
			return branchHash ^ (baseHash << 1);
		}
	};

	// What the committed part of a review is the diff between. Two reviews of the same range
	// share one answer, so this is also what that answer is cached by.
	struct RangeEnds
	{
		Core::Oid MergeBase;
		Core::Oid Tip;

		friend bool operator==(const RangeEnds&, const RangeEnds&) = default;
	};

	struct RangeEndsHash
	{
		std::size_t operator()(const RangeEnds& ends) const noexcept
		{
			const std::size_t mergeBaseHash = std::hash<Core::Oid>{}(ends.MergeBase);
			const std::size_t tipHash = std::hash<Core::Oid>{}(ends.Tip);
			return mergeBaseHash ^ (tipHash << 1);
		}
	};

	// The ordinary answers that leave nothing to diff. A tab says which; none of them is a failure.
	enum class RangeProblem
	{
		None,
		BranchMissing,
		BaseMissing,
		NoCommonAncestor
	};

	struct ReviewRange
	{
		RangeProblem Problem = RangeProblem::None;

		// Read only when Problem is None.
		RangeEnds Ends;

		// Filled whenever the branch exists, so the uncommitted toggle is offered even while the
		// base is missing.
		std::optional<std::filesystem::path> Worktree;

		// The branch's own commits since the merge base, newest first.
		std::vector<Core::CommitSummary> Commits;
		bool CommitsComplete = true;
	};

	// What ReviewModel needs a repository for, and nothing else. Every method is called on a
	// worker thread.
	class ReviewPort
	{
	public:
		virtual ~ReviewPort() = default;

		virtual Core::Result<ReviewRange> ResolveRange(const ReviewKey& key) = 0;

		virtual Core::Result<Core::Diff> QueryRangeDiff(const RangeEnds& ends, Core::WhitespaceInDiff whitespace) = 0;

		// The old side at the merge base, the new side at the tip. A side with an empty path is absent.
		virtual Core::Result<FileTexts> ReadRangeTexts(const RangeEnds& ends,
													   const std::filesystem::path& oldPath,
													   const std::filesystem::path& newPath) = 0;

		// The worktree's files on disk against its HEAD, which is the branch tip.
		virtual Core::Result<Core::Diff> QueryWorktreeDiff(const std::filesystem::path& worktree,
															   Core::WhitespaceInDiff whitespace) = 0;

		virtual Core::Result<FileTexts> ReadWorktreeTexts(const std::filesystem::path& worktree,
														  const std::filesystem::path& oldPath,
														  const std::filesystem::path& newPath) = 0;
	};
}
