#pragma once

#include "Core/Git/Diff.h"
#include "Core/Git/Oid.h"

#include <compare>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>

namespace Graphite {

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

	struct MarkKey
	{
		ReviewGroup Group = ReviewGroup::Commits;
		std::filesystem::path Path;

		friend bool operator==(const MarkKey&, const MarkKey&) = default;
		friend std::strong_ordering operator<=>(const MarkKey&, const MarkKey&) = default;
	};

	struct ViewedMark
	{
		Core::Oid Old;
		Core::Oid New;

		friend bool operator==(const ViewedMark&, const ViewedMark&) = default;
	};

	using BranchMarks = std::map<MarkKey, ViewedMark>;

	using ViewedStateOf = std::function<ViewedState(ReviewGroup group, const Core::FileDiff& file)>;

	ViewedState StateIn(const BranchMarks& marks, ReviewGroup group, const Core::FileDiff& file);
}
