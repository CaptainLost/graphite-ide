#include <doctest/doctest.h>

#include "Editor/ExpectedWrites.h"

TEST_CASE("An expected path is claimed once")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt"});

	CHECK(expected.TakeFor("a.txt"));
	CHECK_FALSE(expected.TakeFor("a.txt"));
}

TEST_CASE("A path nobody expected is not claimed")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt"});

	CHECK_FALSE(expected.TakeFor("b.txt"));
}

TEST_CASE("An expectation expires so it cannot silence a later real change")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt"});

	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds + 0.1f);

	CHECK_FALSE(expected.TakeFor("a.txt"));
	CHECK(expected.GetCount() == 0);
}

TEST_CASE("An expectation survives right up to its deadline")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt"});

	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f);

	CHECK(expected.TakeFor("a.txt"));
}

TEST_CASE("Closing the folder forgets every expectation")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt", "b.txt"});

	expected.Clear();

	CHECK(expected.GetCount() == 0);
	CHECK_FALSE(expected.TakeFor("a.txt"));
}

TEST_CASE("An expectation on a repository-relative path claims the absolute one")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"src/a.txt"});

	CHECK(expected.TakeFor("D:/repo/src/a.txt"));
}

TEST_CASE("An expectation does not claim a file whose name merely ends the same way")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"a.txt"});

	CHECK_FALSE(expected.TakeFor("D:/repo/xa.txt"));
}
