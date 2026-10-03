#include <doctest/doctest.h>

#include "Git/ChangeMarkers.h"

#include <string>
#include <vector>

using Core::LineHunk;
using Graphite::BuildChangeMarkers;
using Graphite::ChangeKind;
using Graphite::DeletionAnchor;
using Graphite::LineChange;

namespace {

	constexpr LineChange kNone{};
	constexpr LineChange kAdded{ChangeKind::Added, false};
	constexpr LineChange kModified{ChangeKind::Modified, false};
	constexpr LineChange kStagedAdded{ChangeKind::Added, true};
	constexpr LineChange kStagedModified{ChangeKind::Modified, true};
}

TEST_CASE("No hunks mark nothing")
{
	const auto markers = BuildChangeMarkers({}, {}, 3);

	CHECK(markers.Lines == std::vector<LineChange>{kNone, kNone, kNone});
	CHECK(markers.Deletions.empty());
}

TEST_CASE("Lines inserted since the index are unstaged additions")
{
	const std::vector<LineHunk> unstaged{{1, 0, 1, 2}};
	const auto markers = BuildChangeMarkers({}, unstaged, 5);

	CHECK(markers.Lines == std::vector<LineChange>{kNone, kAdded, kAdded, kNone, kNone});
}

TEST_CASE("A line replaced since the index is an unstaged modification")
{
	const std::vector<LineHunk> unstaged{{1, 1, 1, 1}};
	const auto markers = BuildChangeMarkers({}, unstaged, 3);

	CHECK(markers.Lines == std::vector<LineChange>{kNone, kModified, kNone});
}

TEST_CASE("A line removed since the index leaves an unstaged anchor")
{
	const std::vector<LineHunk> unstaged{{1, 1, 1, 0}};
	const auto markers = BuildChangeMarkers({}, unstaged, 3);

	CHECK(markers.Deletions == std::vector<DeletionAnchor>{{1, false}});
}

TEST_CASE("A staged change on an untouched line is drawn staged")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 1}};
	const auto markers = BuildChangeMarkers(staged, {}, 4);

	CHECK(markers.Lines == std::vector<LineChange>{kNone, kNone, kStagedModified, kNone});
}

TEST_CASE("A staged change follows its line when unstaged lines are inserted above")
{
	const std::vector<LineHunk> staged{{3, 1, 3, 1}};
	const std::vector<LineHunk> unstaged{{0, 0, 0, 2}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 7);

	CHECK(markers.Lines == std::vector<LineChange>{kAdded, kAdded, kNone, kNone, kNone, kStagedModified, kNone});
}

TEST_CASE("An unstaged change wins over a staged one on the same line")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 1}};
	const std::vector<LineHunk> unstaged{{2, 1, 2, 1}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 4);

	CHECK(markers.Lines[2] == kModified);
}

TEST_CASE("A staged line rewritten since into several lines shows only the unstaged change")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 1}};
	const std::vector<LineHunk> unstaged{{2, 1, 2, 3}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 6);

	CHECK(markers.Lines == std::vector<LineChange>{kNone, kNone, kModified, kModified, kModified, kNone});
}

TEST_CASE("A staged deletion follows its boundary across an unstaged insertion above")
{
	const std::vector<LineHunk> staged{{4, 2, 4, 0}};
	const std::vector<LineHunk> unstaged{{0, 0, 0, 1}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 8);

	CHECK(markers.Deletions == std::vector<DeletionAnchor>{{5, true}});
}

TEST_CASE("A staged deletion inside an unstaged hunk is dropped")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 0}};
	const std::vector<LineHunk> unstaged{{1, 3, 1, 3}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 6);

	CHECK(markers.Deletions.empty());
}

TEST_CASE("A staged deletion where unstaged lines were inserted is dropped")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 0}};
	const std::vector<LineHunk> unstaged{{2, 0, 2, 1}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 5);

	CHECK(markers.Deletions.empty());
	CHECK(markers.Lines[2] == kAdded);
}

TEST_CASE("Two deletions on one boundary leave one unstaged anchor")
{
	const std::vector<LineHunk> staged{{3, 1, 3, 0}};
	const std::vector<LineHunk> unstaged{{3, 2, 3, 0}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 4);

	CHECK(markers.Deletions == std::vector<DeletionAnchor>{{3, false}});
}

TEST_CASE("Deletions at the start and at the end keep their boundaries")
{
	const std::vector<LineHunk> unstaged{{0, 2, 0, 0}, {5, 1, 3, 0}};
	const auto markers = BuildChangeMarkers({}, unstaged, 4);

	CHECK(markers.Deletions == std::vector<DeletionAnchor>{{0, false}, {3, false}});
}

TEST_CASE("A newly staged file is staged additions throughout")
{
	const std::vector<LineHunk> staged{{0, 0, 0, 3}};
	const auto markers = BuildChangeMarkers(staged, {}, 4);

	CHECK(markers.Lines == std::vector<LineChange>{kStagedAdded, kStagedAdded, kStagedAdded, kNone});
}

TEST_CASE("Hunks reaching past the buffer are clamped to it")
{
	const std::vector<LineHunk> staged{{0, 0, 0, 20}};
	const std::vector<LineHunk> unstaged{{0, 0, 0, 10}};
	const auto markers = BuildChangeMarkers(staged, unstaged, 3);

	CHECK(markers.Lines == std::vector<LineChange>{kAdded, kAdded, kAdded});
}

TEST_CASE("An unstaged modification keeps the index lines it replaced")
{
	const std::vector<LineHunk> unstaged{{1, 1, 1, 1}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\n", .Index = "a\nb\nc\n"};
	const auto markers = BuildChangeMarkers({}, unstaged, 4, old);

	REQUIRE(markers.Hunks.size() == 1);
	const Graphite::ChangeHunk& hunk = markers.Hunks.front();
	CHECK(hunk.Kind == Graphite::HunkKind::Modified);
	CHECK_FALSE(hunk.Staged);
	CHECK(hunk.FirstLine == 1);
	CHECK(hunk.LineCount == 1);
	CHECK(hunk.OldCount == 1);
	CHECK(hunk.OldLines == std::vector<std::string>{"b"});

	CHECK(Graphite::FindLineHunk(markers, 1) == &hunk);
	CHECK(Graphite::FindLineHunk(markers, 0) == nullptr);
}

TEST_CASE("An added hunk has no old lines")
{
	const std::vector<LineHunk> unstaged{{1, 0, 1, 2}};
	const Graphite::OldTexts old{.Head = "a\nb\n", .Index = "a\nb\n"};
	const auto markers = BuildChangeMarkers({}, unstaged, 5, old);

	REQUIRE(markers.Hunks.size() == 1);
	CHECK(markers.Hunks.front().Kind == Graphite::HunkKind::Added);
	CHECK(markers.Hunks.front().OldCount == 0);
	CHECK(markers.Hunks.front().OldLines.empty());
	CHECK(Graphite::FindLineHunk(markers, 2) == &markers.Hunks.front());
}

TEST_CASE("A staged deletion keeps the HEAD lines it removed")
{
	const std::vector<LineHunk> staged{{1, 2, 1, 0}};
	const Graphite::OldTexts old{.Head = "a\nx\ny\nb\n", .Index = "a\nb\n"};
	const auto markers = BuildChangeMarkers(staged, {}, 3, old);

	REQUIRE(markers.Hunks.size() == 1);
	const Graphite::ChangeHunk& hunk = markers.Hunks.front();
	CHECK(hunk.Kind == Graphite::HunkKind::Deleted);
	CHECK(hunk.Staged);
	CHECK(hunk.FirstLine == 1);
	CHECK(hunk.OldLines == std::vector<std::string>{"x", "y"});

	CHECK(Graphite::FindDeletionHunk(markers, 1) == &hunk);
	CHECK(Graphite::FindDeletionHunk(markers, 2) == nullptr);
}

TEST_CASE("Old lines are capped but their count is kept")
{
	std::string index;
	for (int line = 0; line < 20; ++line)
		index += std::to_string(line) + "\n";

	const std::vector<LineHunk> unstaged{{0, 20, 0, 0}};
	const Graphite::OldTexts old{.Head = index, .Index = index};
	const auto markers = BuildChangeMarkers({}, unstaged, 1, old);

	REQUIRE(markers.Hunks.size() == 1);
	CHECK(markers.Hunks.front().OldCount == 20);
	REQUIRE(markers.Hunks.front().OldLines.size() == Graphite::kShownOldLines);
	CHECK(markers.Hunks.front().OldLines.front() == "0");
	CHECK(markers.Hunks.front().OldLines.back() == "14");
}

TEST_CASE("A staged hunk is found at the buffer lines it moved to")
{
	const std::vector<LineHunk> staged{{1, 1, 1, 1}};
	const std::vector<LineHunk> unstaged{{0, 0, 0, 2}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\n", .Index = "a\nB\nc\n"};
	const auto markers = BuildChangeMarkers(staged, unstaged, 6, old);

	const Graphite::ChangeHunk* moved = Graphite::FindLineHunk(markers, 3);
	REQUIRE(moved != nullptr);
	CHECK(moved->Staged);
	CHECK(moved->FirstLine == 3);
	CHECK(moved->OldLines == std::vector<std::string>{"b"});

	const Graphite::ChangeHunk* inserted = Graphite::FindLineHunk(markers, 0);
	REQUIRE(inserted != nullptr);
	CHECK_FALSE(inserted->Staged);
	CHECK(inserted->Kind == Graphite::HunkKind::Added);
}

TEST_CASE("A staged hunk hidden entirely by an unstaged one is not listed")
{
	const std::vector<LineHunk> staged{{2, 1, 2, 1}};
	const std::vector<LineHunk> unstaged{{2, 1, 2, 1}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\nd\n", .Index = "a\nb\nC\nd\n"};
	const auto markers = BuildChangeMarkers(staged, unstaged, 5, old);

	REQUIRE(markers.Hunks.size() == 1);
	CHECK_FALSE(markers.Hunks.front().Staged);
	CHECK(markers.Hunks.front().OldLines == std::vector<std::string>{"C"});
}

TEST_CASE("An old last line without a newline is kept")
{
	const std::vector<LineHunk> unstaged{{1, 1, 1, 1}};
	const Graphite::OldTexts old{.Head = "a\nb", .Index = "a\nb"};
	const auto markers = BuildChangeMarkers({}, unstaged, 2, old);

	REQUIRE(markers.Hunks.size() == 1);
	CHECK(markers.Hunks.front().OldLines == std::vector<std::string>{"b"});
}

TEST_CASE("A staged hunk split by an unstaged insertion counts only its own lines")
{
	const std::vector<LineHunk> staged{{1, 2, 1, 2}};
	const std::vector<LineHunk> unstaged{{2, 0, 2, 3}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\nd\n", .Index = "a\nB\nC\nd\n"};
	const auto markers = BuildChangeMarkers(staged, unstaged, 8, old);

	const Graphite::ChangeHunk* hunk = Graphite::FindLineHunk(markers, 1);
	REQUIRE(hunk != nullptr);
	CHECK(hunk->Staged);
	CHECK(hunk->MarkedCount == 2);
	CHECK_FALSE(hunk->Intact);
	CHECK(Graphite::FindLineHunk(markers, 5) == hunk);
}

TEST_CASE("A staged hunk whose first line was rewritten since is not intact")
{
	const std::vector<LineHunk> staged{{1, 2, 1, 2}};
	const std::vector<LineHunk> unstaged{{1, 1, 1, 1}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\nd\n", .Index = "a\nB\nC\nd\n"};
	const auto markers = BuildChangeMarkers(staged, unstaged, 5, old);

	const Graphite::ChangeHunk* hunk = Graphite::FindLineHunk(markers, 2);
	REQUIRE(hunk != nullptr);
	CHECK(hunk->Staged);
	CHECK(hunk->MarkedCount == 1);
	CHECK_FALSE(hunk->Intact);
}

TEST_CASE("An unstaged hunk is intact")
{
	const std::vector<LineHunk> unstaged{{1, 1, 1, 2}};
	const Graphite::OldTexts old{.Head = "a\nb\n", .Index = "a\nb\n"};
	const auto markers = BuildChangeMarkers({}, unstaged, 4, old);

	REQUIRE(markers.Hunks.size() == 1);
	CHECK(markers.Hunks.front().MarkedCount == 2);
	CHECK(markers.Hunks.front().Intact);
}

TEST_CASE("Of two deletions on one boundary the unstaged one is found")
{
	const std::vector<LineHunk> staged{{3, 1, 3, 0}};
	const std::vector<LineHunk> unstaged{{3, 2, 3, 0}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\nh\nd\n", .Index = "a\nb\nc\nd\ne\n"};
	const auto markers = BuildChangeMarkers(staged, unstaged, 4, old);

	const Graphite::ChangeHunk* hunk = Graphite::FindDeletionHunk(markers, 3);
	REQUIRE(hunk != nullptr);
	CHECK_FALSE(hunk->Staged);
	CHECK(hunk->OldLines == std::vector<std::string>{"d", "e"});
}

TEST_CASE("A deletion at the end of the file is found past the last line")
{
	const std::vector<LineHunk> unstaged{{2, 1, 2, 0}};
	const Graphite::OldTexts old{.Head = "a\nb\nc\n", .Index = "a\nb\nc\n"};
	const auto markers = BuildChangeMarkers({}, unstaged, 3, old);

	const Graphite::ChangeHunk* hunk = Graphite::FindDeletionHunk(markers, 2);
	REQUIRE(hunk != nullptr);
	CHECK(hunk->OldLines == std::vector<std::string>{"c"});
}

TEST_CASE("A heading names the kind the count and the shade")
{
	const Graphite::ChangeHunk added{.Kind = Graphite::HunkKind::Added, .Staged = false, .LineCount = 3, .MarkedCount = 3};
	CHECK(Graphite::DescribeHunk(added) == "Added · 3 lines · unstaged");

	const Graphite::ChangeHunk one{.Kind = Graphite::HunkKind::Added, .Staged = true, .LineCount = 1, .MarkedCount = 1};
	CHECK(Graphite::DescribeHunk(one) == "Added · 1 line · staged");
}

TEST_CASE("A heading over old lines says which version they are")
{
	const Graphite::ChangeHunk deleted{.Kind = Graphite::HunkKind::Deleted, .Staged = true, .OldCount = 2, .OldLines = {"a", "b"}};
	CHECK(Graphite::DescribeHunk(deleted) == "Deleted · 2 lines · staged (against HEAD)");

	const Graphite::ChangeHunk modified{.Kind = Graphite::HunkKind::Modified, .Staged = false, .LineCount = 2, .MarkedCount = 2, .OldCount = 2, .OldLines = {"a", "b"}};
	CHECK(Graphite::DescribeHunk(modified) == "Modified · 2 lines · unstaged (against the index)");
}

TEST_CASE("A modification that changed the line count says how many replaced how many")
{
	const Graphite::ChangeHunk grown{.Kind = Graphite::HunkKind::Modified, .Staged = false, .LineCount = 254, .MarkedCount = 254, .OldCount = 2, .OldLines = {"a", "b"}};
	CHECK(Graphite::DescribeHunk(grown) == "Modified · 2 lines replaced by 254 · unstaged (against the index)");

	const Graphite::ChangeHunk shrunk{.Kind = Graphite::HunkKind::Modified, .Staged = true, .LineCount = 1, .MarkedCount = 1, .OldCount = 3, .OldLines = {"a", "b", "c"}};
	CHECK(Graphite::DescribeHunk(shrunk) == "Modified · 3 lines replaced by 1 · staged (against HEAD)");
}
