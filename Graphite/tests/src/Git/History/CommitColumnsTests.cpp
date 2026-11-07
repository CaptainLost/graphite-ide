#include <doctest/doctest.h>

#include "Git/History/CommitColumns.h"

namespace {
	constexpr float kMockupFont = 13.0f;

	Graphite::CommitColumnsInput InputAt(float available, float fontSize = kMockupFont, float graph = 30.0f)
	{
		Graphite::CommitColumnsInput input;
		input.Available = available;
		input.Graph = graph;
		input.FontSize = fontSize;
		return input;
	}
}

TEST_CASE("Git window: a wide history shows the hash")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(900.0f));

	CHECK(columns.ShowHash);
	CHECK(columns.Graph == 30.0f);
}

TEST_CASE("Git window: below 500 mockup pixels the hash is hidden")
{
	CHECK(Graphite::LayoutCommitColumns(InputAt(500.0f)).ShowHash);
	CHECK_FALSE(Graphite::LayoutCommitColumns(InputAt(499.0f)).ShowHash);
}

TEST_CASE("Git window: the hash's threshold grows with the font like the mockup's em")
{
	CHECK(Graphite::LayoutCommitColumns(InputAt(1000.0f, 26.0f)).ShowHash);
	CHECK_FALSE(Graphite::LayoutCommitColumns(InputAt(999.0f, 26.0f)).ShowHash);
}

TEST_CASE("Git window: the author's cell shows the name from room for the avatar and three characters, the avatar alone below it")
{
	constexpr float kAvatarAndGap = 22.0f;
	constexpr float kCharacter = 7.0f;

	CHECK_FALSE(Graphite::ShowsAuthorName(20.0f, kAvatarAndGap, kCharacter));
	CHECK_FALSE(Graphite::ShowsAuthorName(42.5f, kAvatarAndGap, kCharacter));
	CHECK(Graphite::ShowsAuthorName(43.0f, kAvatarAndGap, kCharacter));
	CHECK(Graphite::ShowsAuthorName(162.0f, kAvatarAndGap, kCharacter));
}

TEST_CASE("Git window: the graph is capped to a quarter of the table")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(400.0f, kMockupFont, 600.0f));

	CHECK(columns.Graph == doctest::Approx(100.0f));
}
