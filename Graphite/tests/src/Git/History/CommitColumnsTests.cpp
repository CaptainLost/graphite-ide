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

TEST_CASE("Git window: a wide history shows the author's name and the hash")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(900.0f));

	CHECK(columns.ShowAuthorName);
	CHECK(columns.ShowHash);
	CHECK(columns.Graph == 30.0f);
}

TEST_CASE("Git window: below 640 mockup pixels the author's name is hidden and the hash stays")
{
	CHECK(Graphite::LayoutCommitColumns(InputAt(640.0f)).ShowAuthorName);

	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(639.0f));

	CHECK_FALSE(columns.ShowAuthorName);
	CHECK(columns.ShowHash);
}

TEST_CASE("Git window: below 500 mockup pixels the hash is hidden too")
{
	CHECK(Graphite::LayoutCommitColumns(InputAt(500.0f)).ShowHash);

	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(499.0f));

	CHECK_FALSE(columns.ShowAuthorName);
	CHECK_FALSE(columns.ShowHash);
}

TEST_CASE("Git window: the thresholds grow with the font like the mockup's em")
{
	CHECK(Graphite::LayoutCommitColumns(InputAt(800.0f, 26.0f)).ShowAuthorName == false);
	CHECK(Graphite::LayoutCommitColumns(InputAt(1280.0f, 26.0f)).ShowAuthorName);
	CHECK(Graphite::LayoutCommitColumns(InputAt(1000.0f, 26.0f)).ShowHash);
	CHECK_FALSE(Graphite::LayoutCommitColumns(InputAt(999.0f, 26.0f)).ShowHash);
}

TEST_CASE("Git window: the graph is capped to a quarter of the table")
{
	const Graphite::CommitColumns columns = Graphite::LayoutCommitColumns(InputAt(400.0f, kMockupFont, 600.0f));

	CHECK(columns.Graph == doctest::Approx(100.0f));
}
