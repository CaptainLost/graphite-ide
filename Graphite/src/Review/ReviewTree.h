#pragma once

#include "Review/ReviewFiles.h"
#include "Review/ViewedMarks.h"

#include "Core/Git/Diff.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	enum class ListShape
	{
		Tree,
		Flat
	};

	enum class GroupHeadings
	{
		Shown,
		Hidden
	};

	enum class TreeRowKind
	{
		Heading,
		Folder,
		File
	};

	struct ReviewTreeRow
	{
		TreeRowKind Kind = TreeRowKind::File;
		ReviewGroup Group = ReviewGroup::Commits;

		std::size_t Depth = 0;

		std::string Label;

		std::filesystem::path FolderPath;

		std::size_t FileIndex = 0;

		std::size_t Viewed = 0;
		std::size_t Total = 0;
		std::size_t Shown = 0;

		bool Folded = false;
	};

	struct ReviewTreeInput
	{
		ReviewGroups Groups;

		ViewedStateOf StateOf;

		ListShape Shape = ListShape::Tree;
		GroupHeadings Headings = GroupHeadings::Shown;
		std::string Filter;
		bool HideViewed = false;

		const std::set<MarkKey>* FoldedFolders = nullptr;
	};

	bool MatchesFilter(const std::filesystem::path& path, const std::string& filter);

	std::vector<ReviewTreeRow> BuildReviewTree(const ReviewTreeInput& input);

	const Core::FileDiff& FileOfTreeRow(const ReviewGroups& groups, const ReviewTreeRow& row);

	std::optional<ReviewSelection> FirstFileOf(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows);

	std::vector<std::filesystem::path> ListedPathsOf(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows,
													 ReviewGroup group);

	std::optional<ReviewSelection> FollowSelection(const std::optional<ReviewSelection>& selected,
												   const std::vector<std::filesystem::path>& groupPathsBefore,
												   const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows);

	std::optional<ReviewSelection> NeighbourFileOf(const std::optional<ReviewSelection>& selected,
												   const ReviewGroups& groups,
												   const std::vector<ReviewTreeRow>& rows, ListStep step, ListShape shape);
}
