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

	// A commit's file list has no groups, so no headings either.
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

		// A heading sits at 0; what is under it starts at 1, or at 0 when there are no headings.
		std::size_t Depth = 0;

		// A folder's joined name, "Core/src/Core/Git".
		std::string Label;

		// A folder's full path within the repository: what it is folded by.
		std::filesystem::path FolderPath;

		// A file's index into its group's diff.
		std::size_t FileIndex = 0;

		// A folder's or a heading's files: how many are viewed, how many there are, and - for a
		// heading - how many the filter and Hide viewed leave on screen.
		std::size_t Viewed = 0;
		std::size_t Total = 0;
		std::size_t Shown = 0;

		bool Folded = false;
	};

	struct ReviewTreeInput
	{
		ReviewGroups Groups;

		// Unset for a list without marks, whose files then all read NotViewed.
		std::function<ViewedState(ReviewGroup group, const Core::FileDiff& file)> StateOf;

		ListShape Shape = ListShape::Tree;
		GroupHeadings Headings = GroupHeadings::Shown;
		std::string Filter;
		bool HideViewed = false;

		// The folded folders, by group and full path. Ignored while a filter or Hide viewed is on.
		const std::set<MarkKey>* FoldedFolders = nullptr;
	};

	// A part of the path, whatever its case; an empty filter matches everything.
	bool MatchesFilter(const std::filesystem::path& path, const std::string& filter);

	// The rows the list draws, in order: each present group's heading, then its folders and files
	// (folders first, each by name), or its files by path in the flat shape. A folder is joined with
	// its only child folder when it holds no file; joins are worked out from all of the group's files
	// so a filter never renames a folder. Rebuild only when an input changes.
	std::vector<ReviewTreeRow> BuildReviewTree(const ReviewTreeInput& input);

	const Core::FileDiff& FileOfTreeRow(const ReviewGroups& groups, const ReviewTreeRow& row);

	std::optional<ReviewSelection> FirstFileOf(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows);

	// One file up or down, folders and headings skipped and groups crossed, staying put at either end.
	std::optional<ReviewSelection> NeighbourFileOf(const std::optional<ReviewSelection>& selected,
												   const ReviewGroups& groups,
												   const std::vector<ReviewTreeRow>& rows, ListStep step);
}
