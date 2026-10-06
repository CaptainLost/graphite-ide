#include <doctest/doctest.h>

#include "Git/Changes/ChangesRows.h"

#include <cmath>
#include <filesystem>
#include <optional>
#include <vector>

namespace {

	Core::FileStatus Entry(const char* path, Core::StatusFlags index, Core::StatusFlags workTree)
	{
		Core::FileStatus file;
		file.Path = path;
		file.Index = index;
		file.WorkTree = workTree;
		return file;
	}
}

TEST_CASE("A file staged and edited again appears in both sections")
{
	Core::WorkingTreeStatus status;
	status.Entries.push_back(
		Entry("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified));

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Staged == std::vector<std::size_t>{0});
	CHECK(sections.Unstaged == std::vector<std::size_t>{0});
	CHECK(sections.Conflicted.empty());
}

TEST_CASE("A conflicted file is only conflicted")
{
	Core::WorkingTreeStatus status;
	Core::FileStatus file = Entry("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified);
	file.IsConflicted = true;
	status.Entries.push_back(file);

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Conflicted == std::vector<std::size_t>{0});
	CHECK(sections.Staged.empty());
	CHECK(sections.Unstaged.empty());
}

TEST_CASE("An entry changed only in the working tree is unstaged and nothing else")
{
	Core::WorkingTreeStatus status;
	Core::FileStatus file = Entry("new.txt", Core::StatusFlags::None, Core::StatusFlags::New);
	file.IsUntracked = true;
	status.Entries.push_back(file);

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Unstaged == std::vector<std::size_t>{0});
	CHECK(sections.Staged.empty());
}

TEST_CASE("A clean entry lands in no section at all")
{
	Core::WorkingTreeStatus status;
	status.Entries.push_back(Entry("a.txt", Core::StatusFlags::None, Core::StatusFlags::None));

	const Graphite::ChangeSections sections = Graphite::PartitionStatus(status);

	CHECK(sections.Conflicted.empty());
	CHECK(sections.Staged.empty());
	CHECK(sections.Unstaged.empty());
}

TEST_CASE("The change letter names the most consequential flag")
{
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::New, true) == 'U');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::New, false) == 'A');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Deleted, false) == 'D');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Renamed, false) == 'R');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Modified, false) == 'M');
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::TypeChanged, false) == 'T');

	// libgit2's status.c (index_delta2status and workdir_delta2status) sets each delta
	// status to exactly one bit, except GIT_DELTA_RENAMED which may also set MODIFIED
	// when the old and new blob OIDs differ. diff_tform.c's rename detection rewrites
	// typechange deltas matched to renames as purely RENAMED before status conversion.
	// Thus Renamed+Modified is the only multi-bit combination ever produced. See:
	// vendor/libgit2/src/libgit2/status.c (~lines 25-118) and diff_tform.c (~lines 980-1075).
	CHECK(Graphite::ChangeLetter(Core::StatusFlags::Renamed | Core::StatusFlags::Modified, false) == 'R');
}

TEST_CASE("A selection that is still there stays where it is")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"c.txt", "a.txt", "b.txt"};

	CHECK(Graphite::NextSelection("b.txt", before, after) == std::optional<std::filesystem::path>("b.txt"));
}

TEST_CASE("A selection that left is replaced by the row that took its place")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"a.txt", "c.txt"};

	CHECK(Graphite::NextSelection("b.txt", before, after) == std::optional<std::filesystem::path>("c.txt"));
}

TEST_CASE("A selection on the last row falls back to the new last row")
{
	const std::vector<std::filesystem::path> before{"a.txt", "b.txt", "c.txt"};
	const std::vector<std::filesystem::path> after{"a.txt", "b.txt"};

	CHECK(Graphite::NextSelection("c.txt", before, after) == std::optional<std::filesystem::path>("b.txt"));
}

TEST_CASE("A selection in a section that emptied is dropped")
{
	const std::vector<std::filesystem::path> before{"a.txt"};
	const std::vector<std::filesystem::path> after{};

	CHECK(Graphite::NextSelection("a.txt", before, after) == std::nullopt);
}

TEST_CASE("A selection that was never in the list is dropped")
{
	const std::vector<std::filesystem::path> before{"a.txt"};
	const std::vector<std::filesystem::path> after{"a.txt"};

	CHECK(Graphite::NextSelection("gone.txt", before, after) == std::nullopt);
}

namespace {

	// The one repository these discard cases are asked about. Absolute, because a document's
	// path is absolute and the whole point of the join is to bring the two onto the same
	// footing.
	const std::filesystem::path Repository = "C:/repo";

	Graphite::DiscardSubject File(const char* path, bool untracked)
	{
		Graphite::DiscardSubject subject;
		subject.Path = path;
		subject.IsUntracked = untracked;
		return subject;
	}

	Graphite::DiscardSubject Folder(const char* path,
									std::optional<std::vector<std::filesystem::path>> listing)
	{
		Graphite::DiscardSubject subject;
		subject.Path = path;
		subject.IsDirectory = true;
		subject.IsUntracked = true;
		subject.Listing = std::move(listing);
		return subject;
	}
}

TEST_CASE("Discarding a tracked file restores it and names only itself")
{
	const Graphite::DiscardPlan plan = Graphite::PlanDiscard(File("src/a.txt", false),
															 Repository, {});

	CHECK(plan.Restore == std::vector<std::filesystem::path>{"src/a.txt"});
	CHECK(plan.Delete.empty());
	CHECK(plan.Files == std::vector<std::filesystem::path>{"src/a.txt"});
	CHECK(plan.FileCount == std::nullopt);
}

TEST_CASE("Discarding an untracked file deletes it, since there is nothing to restore")
{
	const Graphite::DiscardPlan plan = Graphite::PlanDiscard(File("src/a.txt", true),
															 Repository, {});

	CHECK(plan.Delete == std::vector<std::filesystem::path>{"src/a.txt"});
	CHECK(plan.Restore.empty());
}

TEST_CASE("A folder discard names the files it was listed with")
{
	const Graphite::DiscardPlan plan = Graphite::PlanDiscard(
		Folder("src", std::vector<std::filesystem::path>{"src/a.txt", "src/deep/b.txt"}),
		Repository, {});

	CHECK(plan.Delete == std::vector<std::filesystem::path>{"src"});
	CHECK(plan.Files == std::vector<std::filesystem::path>{"src/a.txt", "src/deep/b.txt"});
	CHECK(plan.FileCount == std::optional<std::size_t>(2));
}

TEST_CASE("A folder nobody opened names no files and counts none")
{
	const Graphite::DiscardPlan plan = Graphite::PlanDiscard(Folder("src", std::nullopt),
															 Repository, {});

	CHECK(plan.Delete == std::vector<std::filesystem::path>{"src"});
	CHECK(plan.Files.empty());
	CHECK(plan.FileCount == std::nullopt);
}

TEST_CASE("Unsaved edits under a discarded folder are counted as lost")
{
	const std::vector<std::filesystem::path> dirty{"C:/repo/src/a.txt",
												   "C:/repo/src/deep/b.txt"};

	const Graphite::DiscardPlan plan =
		Graphite::PlanDiscard(Folder("src", std::nullopt), Repository, dirty);

	CHECK(plan.Dirty == dirty);
}

TEST_CASE("A folder with an empty listing still answers for the edits under it")
{
	const std::vector<std::filesystem::path> dirty{"C:/repo/src/a.txt"};

	const Graphite::DiscardPlan plan = Graphite::PlanDiscard(
		Folder("src", std::vector<std::filesystem::path>{}), Repository, dirty);

	CHECK(plan.Dirty == dirty);
}

TEST_CASE("A sibling folder sharing a prefix is not inside the one being discarded")
{
	const std::vector<std::filesystem::path> dirty{"C:/repo/src2/a.txt", "C:/repo/srcs.txt"};

	const Graphite::DiscardPlan plan =
		Graphite::PlanDiscard(Folder("src", std::nullopt), Repository, dirty);

	CHECK(plan.Dirty.empty());
}

TEST_CASE("A document under a folder is matched however its path is spelled")
{
	// Native separators, the wrong case, and a step back through a directory: all three are
	// spellings the same document can arrive with.
	const std::vector<std::filesystem::path> dirty{R"(C:\repo\SRC\sub\..\a.txt)"};

	const Graphite::DiscardPlan plan =
		Graphite::PlanDiscard(Folder("src", std::nullopt), Repository, dirty);

	CHECK(plan.Dirty == dirty);
}

TEST_CASE("A file discard warns about that file and no other")
{
	const std::vector<std::filesystem::path> dirty{"C:/repo/src/a.txt", "C:/repo/src/b.txt"};

	const Graphite::DiscardPlan plan =
		Graphite::PlanDiscard(File("src/a.txt", false), Repository, dirty);

	CHECK(plan.Dirty == std::vector<std::filesystem::path>{"C:/repo/src/a.txt"});
}

TEST_CASE("The question counts a folder's files only when they are known")
{
	const Graphite::DiscardSubject listed =
		Folder("src", std::vector<std::filesystem::path>{"src/a.txt", "src/b.txt"});

	CHECK(Graphite::DiscardQuestionText(listed,
										Graphite::PlanDiscard(listed, Repository, {})) == "Delete src/ and the 2 files in it?");

	const Graphite::DiscardSubject unlisted = Folder("src", std::nullopt);

	CHECK(Graphite::DiscardQuestionText(unlisted,
										Graphite::PlanDiscard(unlisted, Repository, {})) == "Delete src/ and everything in it?");
}

TEST_CASE("The question says whether a file is deleted or restored")
{
	const Graphite::DiscardSubject untracked = File("src/a.txt", true);

	CHECK(Graphite::DiscardQuestionText(untracked,
										Graphite::PlanDiscard(untracked, Repository, {})) == "Delete src/a.txt?");

	const Graphite::DiscardSubject tracked = File("src/a.txt", false);

	CHECK(Graphite::DiscardQuestionText(tracked,
										Graphite::PlanDiscard(tracked, Repository, {})) == "Discard the unstaged changes to src/a.txt?");
}

TEST_CASE("A row with room for everything shows the name and the directory whole")
{
	const Graphite::RowText text = Graphite::FitRowText(300.0f, 100.0f, 120.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(100.0f));
	CHECK(text.Directory == doctest::Approx(120.0f));
}

TEST_CASE("A directory that does not fit is cut to what the name leaves over")
{
	const Graphite::RowText text = Graphite::FitRowText(200.0f, 100.0f, 120.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(100.0f));
	CHECK(text.Directory == doctest::Approx(92.0f));
}

TEST_CASE("A directory left less than its minimum is not drawn at all")
{
	const Graphite::RowText text = Graphite::FitRowText(140.0f, 100.0f, 120.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(100.0f));
	CHECK(text.Directory == doctest::Approx(0.0f));
}

TEST_CASE("A directory narrower than the minimum is still drawn when it fits whole")
{
	const Graphite::RowText text = Graphite::FitRowText(140.0f, 100.0f, 20.0f, 8.0f, 40.0f);

	CHECK(text.Directory == doctest::Approx(20.0f));
}

TEST_CASE("A name wider than the row takes the whole row and leaves no directory")
{
	const Graphite::RowText text = Graphite::FitRowText(80.0f, 100.0f, 120.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(80.0f));
	CHECK(text.Directory == doctest::Approx(0.0f));
}

TEST_CASE("A file at the repository root has no directory to fit")
{
	const Graphite::RowText text = Graphite::FitRowText(300.0f, 100.0f, 0.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(100.0f));
	CHECK(text.Directory == doctest::Approx(0.0f));
}

TEST_CASE("A row with no room shows nothing")
{
	const Graphite::RowText text = Graphite::FitRowText(-10.0f, 100.0f, 120.0f, 8.0f, 40.0f);

	CHECK(text.Name == doctest::Approx(0.0f));
	CHECK(text.Directory == doctest::Approx(0.0f));
}

namespace {

	const Graphite::FolderRowGlyphs ClosedFolderGlyphs{
		.Folder = Core::GlyphInk{.StartFromDrawX = 1.0f, .EndFromDrawX = 15.0f},
		.Chevron = Core::GlyphInk{.StartFromDrawX = 6.0f, .EndFromDrawX = 10.0f}};

	constexpr float FileRowIconInkToNameGap = 10.0f;
	constexpr float NameSideBearing = 1.0f;
	constexpr float FileRowVisibleGap = FileRowIconInkToNameGap + NameSideBearing;

	float VisibleGapAfterFolder(const Graphite::FolderRowPlacement& placement,
								const Graphite::FolderRowGlyphs& drawn)
	{
		return placement.ChevronOffsetFromIcon + drawn.Chevron.StartFromDrawX - drawn.Folder.EndFromDrawX;
	}

	float VisibleGapBeforeName(const Graphite::FolderRowPlacement& placement,
							   const Graphite::FolderRowGlyphs& drawn)
	{
		return placement.NameOffsetFromIcon + NameSideBearing -
			   (placement.ChevronOffsetFromIcon + drawn.Chevron.EndFromDrawX);
	}
}

TEST_CASE("A closed folder row leaves a file row's gap between the folder and the chevron, and again before the name")
{
	const Graphite::FolderRowPlacement placement = Graphite::PlaceFolderRow(
		ClosedFolderGlyphs, ClosedFolderGlyphs, FileRowIconInkToNameGap, NameSideBearing);

	CHECK(VisibleGapAfterFolder(placement, ClosedFolderGlyphs) == doctest::Approx(FileRowVisibleGap));
	CHECK(VisibleGapBeforeName(placement, ClosedFolderGlyphs) == doctest::Approx(FileRowVisibleGap));
}

TEST_CASE("An open folder keeps its name where the closed one had it and centres its wider chevron")
{
	const Graphite::FolderRowGlyphs openFolderGlyphs{
		.Folder = Core::GlyphInk{.StartFromDrawX = 1.0f, .EndFromDrawX = 16.0f},
		.Chevron = Core::GlyphInk{.StartFromDrawX = 4.0f, .EndFromDrawX = 11.0f}};

	const Graphite::FolderRowPlacement closedPlacement = Graphite::PlaceFolderRow(
		ClosedFolderGlyphs, ClosedFolderGlyphs, FileRowIconInkToNameGap, NameSideBearing);
	const Graphite::FolderRowPlacement openPlacement = Graphite::PlaceFolderRow(
		ClosedFolderGlyphs, openFolderGlyphs, FileRowIconInkToNameGap, NameSideBearing);

	CHECK(openPlacement.NameOffsetFromIcon == doctest::Approx(closedPlacement.NameOffsetFromIcon));
	CHECK(VisibleGapAfterFolder(openPlacement, openFolderGlyphs) ==
		  doctest::Approx(VisibleGapBeforeName(openPlacement, openFolderGlyphs)));
}

TEST_CASE("An open folder's file icons are centred under its chevron")
{
	const Graphite::FolderRowPlacement placement{.ChevronOffsetFromIcon = 21.0f, .NameOffsetFromIcon = 40.0f};
	const Core::GlyphInk downChevron{.StartFromDrawX = 4.0f, .EndFromDrawX = 11.0f};
	constexpr float iconColumnWidth = 15.0f;

	const float childIndent = Graphite::FolderChildIndent(placement, downChevron, iconColumnWidth);

	const float chevronInkCentre =
		placement.ChevronOffsetFromIcon + (downChevron.StartFromDrawX + downChevron.EndFromDrawX) / 2.0f;
	const float childIconColumnCentre = childIndent + iconColumnWidth / 2.0f;

	CHECK(childIconColumnCentre == doctest::Approx(chevronInkCentre));
}

TEST_CASE("A folder row's offsets are whole pixels, since ImGui truncates the x it draws text at")
{
	const Graphite::FolderRowGlyphs fractionalGlyphs{
		.Folder = Core::GlyphInk{.StartFromDrawX = 0.75f, .EndFromDrawX = 14.25f},
		.Chevron = Core::GlyphInk{.StartFromDrawX = 4.75f, .EndFromDrawX = 10.25f}};

	const Graphite::FolderRowPlacement placement =
		Graphite::PlaceFolderRow(fractionalGlyphs, fractionalGlyphs, 9.75f, 1.25f);

	CHECK(placement.ChevronOffsetFromIcon == std::round(placement.ChevronOffsetFromIcon));
	CHECK(placement.NameOffsetFromIcon == std::round(placement.NameOffsetFromIcon));
}
