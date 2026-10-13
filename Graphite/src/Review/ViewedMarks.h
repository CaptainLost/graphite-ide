#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"

#include <compare>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>

namespace Graphite {

	// The two lists a review shows: what the branch's commits changed since the merge base, and
	// what its worktree changed on top of the tip.
	enum class ReviewGroup
	{
		Commits,
		Uncommitted
	};

	inline constexpr std::size_t kReviewGroupCount = 2;

	inline std::size_t IndexOf(ReviewGroup group)
	{
		return static_cast<std::size_t>(group);
	}

	enum class ViewedState
	{
		NotViewed,
		Viewed,
		ChangedSinceViewed
	};

	// Where a mark sits: one file of one group, by the path the list shows it under - the new path,
	// or the old one for a delete.
	struct MarkKey
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const MarkKey&, const MarkKey&) = default;
		friend std::strong_ordering operator<=>(const MarkKey&, const MarkKey&) = default;
	};

	// The file as the reader looked at it: the pair of blob ids names that exact change.
	struct ViewedMark
	{
		Core::Oid Old;
		Core::Oid New;

		friend bool operator==(const ViewedMark&, const ViewedMark&) = default;
	};

	// One branch's marks, ordered by key so a row finds its mark without walking the others.
	using BranchMarks = std::map<MarkKey, ViewedMark>;

	// Says how far a file of a group has been looked at.
	using ViewedStateOf = std::function<ViewedState(ReviewGroup group, const Core::FileDiff& file)>;

	ViewedState StateIn(const BranchMarks& marks, ReviewGroup group, const Core::FileDiff& file);
}
