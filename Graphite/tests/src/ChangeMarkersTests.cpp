#include <doctest/doctest.h>

#include "Git/ChangeMarkers.h"

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

	CHECK(markers.Lines
		  == std::vector<LineChange>{kAdded, kAdded, kNone, kNone, kNone, kStagedModified, kNone});
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

	CHECK(markers.Lines
		  == std::vector<LineChange>{kNone, kNone, kModified, kModified, kModified, kNone});
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
