#include <doctest/doctest.h>

#include "Git/Changes/CommitBlocker.h"

using namespace Graphite;

namespace {

	CommitBlockerInputs Ready()
	{
		return {.AnythingStaged = true, .MessageIsEmpty = false};
	}
}

TEST_CASE("A staged change and a message leave the commit enabled")
{
	CHECK_FALSE(WhyCommitIsDisabled(Ready()).has_value());
}

TEST_CASE("A write in flight blocks the commit before anything else")
{
	CommitBlockerInputs inputs = Ready();
	inputs.WriteInFlight = true;

	CHECK(WhyCommitIsDisabled(inputs) == "A write is in progress");

	inputs.AnythingStaged = false;
	inputs.Amend = true;

	CHECK(WhyCommitIsDisabled(inputs) == "A write is in progress");
}

TEST_CASE("A message with nothing staged is told to stage")
{
	CommitBlockerInputs inputs = Ready();
	inputs.AnythingStaged = false;

	CHECK(WhyCommitIsDisabled(inputs) == "Stage changes to commit");
}

TEST_CASE("Nothing staged and no message is told to stage first")
{
	CommitBlockerInputs inputs;

	CHECK(WhyCommitIsDisabled(inputs) == "Stage changes to commit");
}

TEST_CASE("A staged change without a message is told to type one")
{
	CommitBlockerInputs inputs = Ready();
	inputs.MessageIsEmpty = true;

	CHECK(WhyCommitIsDisabled(inputs) == "Type a commit message");
}

TEST_CASE("An amend needs neither a staged change nor a message")
{
	CommitBlockerInputs inputs;
	inputs.Amend = true;

	CHECK_FALSE(WhyCommitIsDisabled(inputs).has_value());
}

TEST_CASE("A merge needs no staged change but still needs a message")
{
	CommitBlockerInputs inputs;
	inputs.Merging = true;

	CHECK(WhyCommitIsDisabled(inputs) == "Type a commit message");

	inputs.MessageIsEmpty = false;

	CHECK_FALSE(WhyCommitIsDisabled(inputs).has_value());
}
