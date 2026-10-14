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

		RangeEnds Ends;

		std::optional<std::filesystem::path> Worktree;

		std::vector<Core::CommitSummary> Commits;
		bool CommitsComplete = true;
	};

	struct SinceViewedRequest
	{
		Core::Oid ViewedBlob;
		Core::Oid CurrentBlob;
		std::filesystem::path Path;
		Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;
		std::optional<Core::Oid> Tip;
		std::optional<std::filesystem::path> Worktree;
	};

	struct SinceViewedKey
	{
		Core::Oid ViewedBlob;
		Core::Oid CurrentBlob;
		std::filesystem::path Path;
		Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;

		friend bool operator==(const SinceViewedKey&, const SinceViewedKey&) = default;
	};

	struct SinceViewedKeyHash
	{
		std::size_t operator()(const SinceViewedKey& key) const noexcept
		{
			const std::size_t viewedHash = std::hash<Core::Oid>{}(key.ViewedBlob);
			const std::size_t currentHash = std::hash<Core::Oid>{}(key.CurrentBlob);
			const std::size_t pathHash = std::hash<std::filesystem::path>{}(key.Path);
			const std::size_t whitespaceHash = static_cast<std::size_t>(key.Whitespace);
			return viewedHash ^ (currentHash << 1) ^ (pathHash << 2) ^ (whitespaceHash << 3);
		}
	};

	inline SinceViewedKey KeyOf(const SinceViewedRequest& request)
	{
		return SinceViewedKey{.ViewedBlob = request.ViewedBlob,
							  .CurrentBlob = request.CurrentBlob,
							  .Path = request.Path,
							  .Whitespace = request.Whitespace};
	}

	struct SinceViewedAnswer
	{
		Core::FileDiff Diff;
		FileTexts Texts;
	};

	// WARNING: Every method is called on a worker thread.
	class ReviewPort
	{
	public:
		virtual ~ReviewPort() = default;

		virtual Core::Result<ReviewRange> ResolveRange(const ReviewKey& key) = 0;

		// WARNING: A zero MergeBase is the empty tree: a root commit's own change.
		virtual Core::Result<Core::Diff> QueryRangeDiff(const RangeEnds& ends, Core::WhitespaceInDiff whitespace) = 0;

		virtual Core::Result<FileTexts> ReadRangeTexts(const RangeEnds& ends,
													   const std::filesystem::path& oldPath,
													   const std::filesystem::path& newPath) = 0;

		virtual Core::Result<Core::Diff> QueryWorktreeDiff(const std::filesystem::path& worktree,
														   Core::WhitespaceInDiff whitespace) = 0;

		virtual Core::Result<FileTexts> ReadWorktreeTexts(const std::filesystem::path& worktree,
														  const std::filesystem::path& oldPath,
														  const std::filesystem::path& newPath) = 0;

		// WARNING: NotFound means the viewed blob is no longer in the repository, and only that.
		virtual Core::Result<SinceViewedAnswer> ReadSinceViewed(const SinceViewedRequest& request) = 0;
	};
}
