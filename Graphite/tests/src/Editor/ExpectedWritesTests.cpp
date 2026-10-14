#include <doctest/doctest.h>

#include "Editor/ExpectedWrites.h"

TEST_CASE("An expected path is claimed once")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	CHECK(expected.TakeFor("D:\\repo\\a.txt"));
	CHECK_FALSE(expected.TakeFor("D:\\repo\\a.txt"));
}

TEST_CASE("A path nobody expected is not claimed")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	CHECK_FALSE(expected.TakeFor("D:\\repo\\b.txt"));
}

TEST_CASE("An expectation expires so it cannot silence a later real change")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	expected.Tick(Core::Timestep{Graphite::ExpectedWrites::kExpectedWriteSeconds + 0.1f});

	CHECK_FALSE(expected.TakeFor("D:\\repo\\a.txt"));
	CHECK(expected.GetCount() == 0);
}

TEST_CASE("An expectation survives right up to its deadline")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	expected.Tick(Core::Timestep{Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f});

	CHECK(expected.TakeFor("D:\\repo\\a.txt"));
}

TEST_CASE("Closing the folder forgets every expectation")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt", "D:\\repo\\b.txt"});

	expected.Clear();

	CHECK(expected.GetCount() == 0);
	CHECK_FALSE(expected.TakeFor("D:\\repo\\a.txt"));
}

TEST_CASE("An absolute path with native separators is claimed")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\src\\a.txt"});

	CHECK(expected.TakeFor("D:\\repo\\src\\a.txt"));
}

TEST_CASE("An expectation does not claim a different file of the same name in a subdirectory")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	CHECK_FALSE(expected.TakeFor("D:\\repo\\src\\a.txt"));
}

TEST_CASE("An expectation is claimed regardless of case, since Windows paths are")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\src\\A.txt"});

	CHECK(expected.TakeFor("D:\\repo\\src\\a.txt"));
}

TEST_CASE("An expectation spelled with forward slashes claims a query spelled with backslashes")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:/repo/src/a.txt"});

	CHECK(expected.TakeFor("D:\\repo\\src\\a.txt"));
}

TEST_CASE("An expectation with a redundant parent segment claims the path it resolves to")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\src\\..\\a.txt"});

	CHECK(expected.TakeFor("D:\\repo\\a.txt"));
}

TEST_CASE("Expecting the same path twice resets its age instead of adding a second entry")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	expected.Tick(Core::Timestep{Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f});

	expected.Expect({"D:\\repo\\a.txt"});

	CHECK(expected.GetCount() == 1);

	expected.Tick(Core::Timestep{Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f});

	CHECK(expected.TakeFor("D:\\repo\\a.txt"));
}
