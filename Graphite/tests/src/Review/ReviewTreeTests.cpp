#include <doctest/doctest.h>

#include "Review/ReviewTree.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

using Graphite::GroupHeadings;
using Graphite::ListShape;
using Graphite::ListStep;
using Graphite::ReviewGroup;
using Graphite::ReviewGroups;
using Graphite::ReviewSelection;
using Graphite::ReviewTreeInput;
using Graphite::ReviewTreeRow;
using Graphite::TreeRowKind;
using Graphite::ViewedState;

namespace {

	Core::Diff DiffOfFiles(const std::vector<std::filesystem::path>& paths)
	{
		Core::Diff diff;

		for (const std::filesystem::path& path : paths)
		{
			Core::FileDiff file;
			file.Change = Core::FileChange::Modified;
			file.OldPath = path;
			file.NewPath = path;
			diff.Files.push_back(file);
		}

		return diff;
	}

	// "Folder Core/src/Core/Git @1", "File Diff.h @2", "Heading @0" - what a test compares rows by.
	std::vector<std::string> Describe(const ReviewGroups& groups, const std::vector<ReviewTreeRow>& rows)
	{
		std::vector<std::string> lines;

		for (const ReviewTreeRow& row : rows)
		{
			std::string line;

			switch (row.Kind)
			{
			case TreeRowKind::Heading:
				line = "Heading";
				break;
			case TreeRowKind::Folder:
				line = "Folder " + row.Label;
				break;
			case TreeRowKind::File:
				line = "File " + Graphite::FileOfTreeRow(groups, row).NewPath.filename().generic_string();
				break;
			}

			lines.push_back(line + " @" + std::to_string(row.Depth));
		}

		return lines;
	}

	ViewedState ViewedIfNamed(const Core::FileDiff& file, const std::string& name)
	{
		return file.NewPath.filename() == std::filesystem::path{name} ? ViewedState::Viewed : ViewedState::NotViewed;
	}
}

TEST_CASE("A chain of folders with one child each is joined into one folder row")
{
	const Core::Diff committed = DiffOfFiles({"Core/src/Core/Git/Repository.h", "Core/src/Core/Git/Diff.h"});
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups});

	CHECK(Describe(groups, rows) == std::vector<std::string>{"Heading @0", "Folder Core/src/Core/Git @1",
															 "File Diff.h @2", "File Repository.h @2"});
	CHECK(rows[1].FolderPath == std::filesystem::path{"Core/src/Core/Git"});
}

TEST_CASE("Joining stops at a folder with files or more than one child and folders come before files")
{
	const Core::Diff committed = DiffOfFiles({"a/z.h", "a/c/y.h", "a/b/x.h"});
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups});

	CHECK(Describe(groups, rows) == std::vector<std::string>{"Heading @0", "Folder a @1", "Folder b @2", "File x.h @3",
															 "Folder c @2", "File y.h @3", "File z.h @2"});
}

TEST_CASE("A folder counts the viewed files below it out of all of them")
{
	const Core::Diff committed = DiffOfFiles({"a/b/x.h", "a/c/y.h", "a/z.h"});
	const ReviewGroups groups{.Commits = &committed};
	ReviewTreeInput input{.Groups = groups};
	input.StateOf = [](ReviewGroup, const Core::FileDiff& file) { return ViewedIfNamed(file, "x.h"); };

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(input);

	REQUIRE(rows.size() == 7);
	CHECK(rows[0].Viewed == 1);
	CHECK(rows[0].Total == 3);
	CHECK(rows[1].Viewed == 1);
	CHECK(rows[1].Total == 3);
	CHECK(rows[2].Viewed == 1);
	CHECK(rows[2].Total == 1);
}

TEST_CASE("A folded folder keeps its row and hides what is below it until a filter is on")
{
	const Core::Diff committed = DiffOfFiles({"a/b/x.h", "a/c/y.h"});
	const ReviewGroups groups{.Commits = &committed};
	const std::set<Graphite::MarkKey> folded = {Graphite::MarkKey{.Group = ReviewGroup::Commits, .Path = "a/b"}};
	ReviewTreeInput input{.Groups = groups, .FoldedFolders = &folded};

	const std::vector<ReviewTreeRow> foldedRows = Graphite::BuildReviewTree(input);
	CHECK(Describe(groups, foldedRows) ==
		  std::vector<std::string>{"Heading @0", "Folder a @1", "Folder b @2", "Folder c @2", "File y.h @3"});
	CHECK(foldedRows[2].Folded);

	input.Filter = ".h";
	const std::vector<ReviewTreeRow> filteredRows = Graphite::BuildReviewTree(input);
	CHECK(Describe(groups, filteredRows) == std::vector<std::string>{"Heading @0", "Folder a @1", "Folder b @2",
																	 "File x.h @3", "Folder c @2", "File y.h @3"});
}

TEST_CASE("The filter keeps matching files and their folders and the heading counts what it shows")
{
	const Core::Diff committed = DiffOfFiles({"Graphite/src/Review/ReviewTree.cpp", "Core/src/Core/Git/Diff.h"});
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups, .Filter = "REVIEW/"});

	CHECK(Describe(groups, rows) ==
		  std::vector<std::string>{"Heading @0", "Folder Graphite/src/Review @1", "File ReviewTree.cpp @2"});
	CHECK(rows[0].Shown == 1);
	CHECK(rows[0].Total == 2);
}

TEST_CASE("A filter that hides siblings leaves the joined folder names as they were")
{
	const Core::Diff committed = DiffOfFiles({"a/b/x.h", "a/c/y.h"});
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups, .Filter = "x.h"});

	CHECK(Describe(groups, rows) == std::vector<std::string>{"Heading @0", "Folder a @1", "Folder b @2", "File x.h @3"});
}

TEST_CASE("Hide viewed leaves out viewed files and keeps the ones changed since")
{
	const Core::Diff committed = DiffOfFiles({"x.h", "y.h", "z.h"});
	const ReviewGroups groups{.Commits = &committed};
	ReviewTreeInput input{.Groups = groups, .HideViewed = true};
	input.StateOf = [](ReviewGroup, const Core::FileDiff& file) {
		return file.NewPath == std::filesystem::path{"x.h"} ? ViewedState::Viewed
			   : file.NewPath == std::filesystem::path{"y.h"} ? ViewedState::ChangedSinceViewed
																: ViewedState::NotViewed;
	};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(input);

	CHECK(Describe(groups, rows) == std::vector<std::string>{"Heading @0", "File y.h @1", "File z.h @1"});
}

TEST_CASE("The flat shape lists files by path without folders")
{
	const Core::Diff committed = DiffOfFiles({"b/y.h", "a/x.h"});
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows =
		Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups, .Shape = ListShape::Flat});

	CHECK(Describe(groups, rows) == std::vector<std::string>{"Heading @0", "File x.h @1", "File y.h @1"});
}

TEST_CASE("Without headings a list starts at depth zero and a folded group shows only its heading")
{
	const Core::Diff committed = DiffOfFiles({"a/x.h"});

	const ReviewGroups plain{.Commits = &committed};
	const std::vector<ReviewTreeRow> unheaded =
		Graphite::BuildReviewTree(ReviewTreeInput{.Groups = plain, .Headings = GroupHeadings::Hidden});
	CHECK(Describe(plain, unheaded) == std::vector<std::string>{"Folder a @0", "File x.h @1"});

	ReviewGroups collapsed{.Commits = &committed};
	collapsed.Collapsed[Graphite::IndexOf(ReviewGroup::Commits)] = true;
	const std::vector<ReviewTreeRow> folded = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = collapsed});
	CHECK(Describe(collapsed, folded) == std::vector<std::string>{"Heading @0"});
}

TEST_CASE("The first file and the arrow keys walk files across groups and skip folders")
{
	const Core::Diff committed = DiffOfFiles({"a/x.h", "a/y.h"});
	const Core::Diff uncommitted = DiffOfFiles({"b/z.h"});
	const ReviewGroups groups{.Commits = &committed, .Uncommitted = &uncommitted};
	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups});
	const ReviewSelection first{.Group = ReviewGroup::Commits, .Path = "a/x.h"};
	const ReviewSelection last{.Group = ReviewGroup::Uncommitted, .Path = "b/z.h"};

	CHECK(Graphite::FirstFileOf(groups, rows) == std::optional<ReviewSelection>{first});
	CHECK(Graphite::NeighbourFileOf(ReviewSelection{.Group = ReviewGroup::Commits, .Path = "a/y.h"}, groups, rows,
									ListStep::Next, ListShape::Tree) == std::optional<ReviewSelection>{last});
	CHECK(Graphite::NeighbourFileOf(last, groups, rows, ListStep::Next, ListShape::Tree) ==
		  std::optional<ReviewSelection>{last});
	CHECK(Graphite::NeighbourFileOf(first, groups, rows, ListStep::Previous, ListShape::Tree) ==
		  std::optional<ReviewSelection>{first});
}

TEST_CASE("A selection the list no longer shows steps to the files on either side of where it would stand")
{
	const Core::Diff committed = DiffOfFiles({"lib/z.h", "src/a.h", "src/b.h", "src/c.h", "on.h"});
	const ReviewGroups groups{.Commits = &committed};
	const ReviewSelection fileZ{.Group = ReviewGroup::Commits, .Path = "lib/z.h"};
	const ReviewSelection fileA{.Group = ReviewGroup::Commits, .Path = "src/a.h"};
	const ReviewSelection fileB{.Group = ReviewGroup::Commits, .Path = "src/b.h"};
	const ReviewSelection fileC{.Group = ReviewGroup::Commits, .Path = "src/c.h"};
	const ReviewSelection fileOn{.Group = ReviewGroup::Commits, .Path = "on.h"};

	ReviewTreeInput hidingB{.Groups = groups, .HideViewed = true};
	hidingB.StateOf = [](ReviewGroup, const Core::FileDiff& file) { return ViewedIfNamed(file, "b.h"); };
	const std::vector<ReviewTreeRow> withoutB = Graphite::BuildReviewTree(hidingB);

	CHECK(Graphite::NeighbourFileOf(fileB, groups, withoutB, ListStep::Next, ListShape::Tree) ==
		  std::optional<ReviewSelection>{fileC});
	CHECK(Graphite::NeighbourFileOf(fileB, groups, withoutB, ListStep::Previous, ListShape::Tree) ==
		  std::optional<ReviewSelection>{fileA});

	ReviewTreeInput hidingZ{.Groups = groups, .HideViewed = true};
	hidingZ.StateOf = [](ReviewGroup, const Core::FileDiff& file) { return ViewedIfNamed(file, "z.h"); };
	const std::vector<ReviewTreeRow> withoutZ = Graphite::BuildReviewTree(hidingZ);

	CHECK(Graphite::NeighbourFileOf(fileZ, groups, withoutZ, ListStep::Previous, ListShape::Tree) ==
		  std::optional<ReviewSelection>{fileZ});

	const std::set<Graphite::MarkKey> folded = {Graphite::MarkKey{.Group = ReviewGroup::Commits, .Path = "src"}};
	const std::vector<ReviewTreeRow> srcFolded =
		Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups, .FoldedFolders = &folded});

	CHECK(Graphite::NeighbourFileOf(fileB, groups, srcFolded, ListStep::Next, ListShape::Tree) ==
		  std::optional<ReviewSelection>{fileOn});
	CHECK(Graphite::NeighbourFileOf(fileB, groups, srcFolded, ListStep::Previous, ListShape::Tree) ==
		  std::optional<ReviewSelection>{fileZ});

	ReviewTreeInput flatHidingA{.Groups = groups, .Shape = ListShape::Flat, .HideViewed = true};
	flatHidingA.StateOf = [](ReviewGroup, const Core::FileDiff& file) { return ViewedIfNamed(file, "a.h"); };
	const std::vector<ReviewTreeRow> flatWithoutA = Graphite::BuildReviewTree(flatHidingA);

	CHECK(Graphite::NeighbourFileOf(fileA, groups, flatWithoutA, ListStep::Previous, ListShape::Flat) ==
		  std::optional<ReviewSelection>{fileOn});
	CHECK(Graphite::NeighbourFileOf(fileA, groups, flatWithoutA, ListStep::Next, ListShape::Flat) ==
		  std::optional<ReviewSelection>{fileB});
}

TEST_CASE("The filter matches a part of the path whatever its case")
{
	CHECK(Graphite::MatchesFilter("Graphite/src/Review/ReviewTree.cpp", "review/"));
	CHECK(Graphite::MatchesFilter("a.cpp", ""));
	CHECK_FALSE(Graphite::MatchesFilter("a.cpp", "b"));
}

TEST_CASE("A folder with thousands of sibling folders is listed in full and in name order")
{
	constexpr std::size_t siblingCount = 3000;
	std::vector<std::filesystem::path> paths;

	for (std::size_t number = 0; number < siblingCount; ++number)
	{
		paths.push_back(std::filesystem::path{"cases"} / ("c" + std::to_string(number)) / "x.h");
	}

	const Core::Diff committed = DiffOfFiles(paths);
	const ReviewGroups groups{.Commits = &committed};

	const std::vector<ReviewTreeRow> rows = Graphite::BuildReviewTree(ReviewTreeInput{.Groups = groups});

	REQUIRE(rows.size() == 1 + 1 + 2 * siblingCount);
	CHECK(rows[1].Label == "cases");
	CHECK(rows[2].Label == "c0");
	CHECK(rows[4].Label == "c1");
	CHECK(rows[6].Label == "c10");
	CHECK(rows[1].Total == siblingCount);
}
