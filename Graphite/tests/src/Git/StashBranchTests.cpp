#include <doctest/doctest.h>

#include "Git/StashBranch.h"

TEST_CASE("A stash made with a message names its branch after On")
{
	CHECK(Graphite::StashBranch("On main: half-done refactor") == "main");
}

TEST_CASE("A stash made without a message names its branch after WIP on")
{
	CHECK(Graphite::StashBranch("WIP on main: 1a2b3c4 Add the parser") == "main");
}

TEST_CASE("A branch name with a slash is kept whole")
{
	CHECK(Graphite::StashBranch("On feature/x: try this") == "feature/x");
	CHECK(Graphite::StashBranch("WIP on feature/x: 1a2b3c4 Summary: with a colon") == "feature/x");
}

TEST_CASE("A stash message without git's prefix names no branch")
{
	CHECK_FALSE(Graphite::StashBranch("just a message").has_value());
	CHECK_FALSE(Graphite::StashBranch("On main without a colon").has_value());
	CHECK_FALSE(Graphite::StashBranch("On : empty").has_value());
	CHECK_FALSE(Graphite::StashBranch("").has_value());
}

TEST_CASE("A message whose colon follows a space is not taken for a branch")
{
	CHECK_FALSE(Graphite::StashBranch("On second thought: keep this").has_value());
}

TEST_CASE("A stash made with a message is labelled by that message alone")
{
	const Graphite::StashLabel label = Graphite::StashRowLabel("On main: tmp", "2026-10-02 14:32");

	CHECK(label.Text == "tmp");
	CHECK(label.Named);
}

TEST_CASE("A stash made without a message is labelled by when and where, not by git's commit")
{
	const Graphite::StashLabel label = Graphite::StashRowLabel(
		"WIP on refactor/modal-question: 8dde83d Ask once", "2026-10-02 14:32");

	CHECK(label.Text == "2026-10-02 14:32 - refactor/modal-question");
	CHECK_FALSE(label.Named);
}

TEST_CASE("A stash given an empty message is labelled as one with none")
{
	const Graphite::StashLabel label = Graphite::StashRowLabel("On main:   ", "2026-10-02 14:32");

	CHECK(label.Text == "2026-10-02 14:32 - main");
	CHECK_FALSE(label.Named);
}

TEST_CASE("A stash message without git's prefix is shown as it is")
{
	const Graphite::StashLabel label = Graphite::StashRowLabel("On second thought: keep this", "2026-10-02");

	CHECK(label.Text == "On second thought: keep this");
	CHECK(label.Named);
}

TEST_CASE("A stash with no message at all is labelled by its date alone")
{
	const Graphite::StashLabel label = Graphite::StashRowLabel("", "2026-10-02 14:32");

	CHECK(label.Text == "2026-10-02 14:32");
	CHECK_FALSE(label.Named);
}

TEST_CASE("Only the first line of a stash message makes the label")
{
	CHECK(Graphite::StashRowLabel("On main: first\nsecond", "2026-10-02").Text == "first");
}

TEST_CASE("A stash made on a detached head names no branch but keeps its message")
{
	CHECK(Graphite::StashBranch("On (no branch): tmp") == "(no branch)");

	const Graphite::StashLabel named = Graphite::StashRowLabel("On (no branch): tmp", "2026-10-02");
	CHECK(named.Text == "tmp");
	CHECK(named.Named);

	const Graphite::StashLabel unnamed =
		Graphite::StashRowLabel("WIP on (no branch): 1a2b3c4 Add the parser", "2026-10-02");
	CHECK(unnamed.Text == "2026-10-02 - (no branch)");
	CHECK_FALSE(unnamed.Named);
}

TEST_CASE("A stash message is trimmed of tabs and a carriage return as well as spaces")
{
	CHECK(Graphite::StashRowLabel("On main:\ttmp \r\nmore", "2026-10-02").Text == "tmp");
	CHECK_FALSE(Graphite::StashRowLabel("On main: \t\r", "2026-10-02").Named);
}
