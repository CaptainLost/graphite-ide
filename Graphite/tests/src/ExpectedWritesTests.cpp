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

	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds + 0.1f);

	CHECK_FALSE(expected.TakeFor("D:\\repo\\a.txt"));
	CHECK(expected.GetCount() == 0);
}

TEST_CASE("An expectation survives right up to its deadline")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:\\repo\\a.txt"});

	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f);

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

// Regression: whole-path equality, not a filename or suffix match. A root-level expectation
// must not claim a different file of the same name in a subdirectory - the earlier suffix rule
// let it, which meant a genuine external change to a dirty buffer could be reloaded with no
// prompt at all.
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

// Regression: nothing else in this file writes different separators on the two sides, so this
// is the only case that fails if make_preferred() is dropped from Normalize(). The expectation
// is spelled the way libgit2 reports it - forward slashes past the root - joined onto a Windows
// drive root; the query is spelled the way directory_iterator yields it, all backslashes. Both
// name the same file.
TEST_CASE("An expectation spelled with forward slashes claims a query spelled with backslashes")
{
	Graphite::ExpectedWrites expected;
	expected.Expect({"D:/repo/src/a.txt"});

	CHECK(expected.TakeFor("D:\\repo\\src\\a.txt"));
}

// Regression: nothing else in this file exercises lexically_normal, so this is the only case
// that fails if that step is dropped from Normalize().
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

	// Most of the way to the first registration's deadline ...
	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f);

	// ... and re-expected, which must reset its age rather than add a second entry.
	expected.Expect({"D:\\repo\\a.txt"});

	CHECK(expected.GetCount() == 1);

	// Past what would have been the first registration's deadline, but well within the
	// second's - proving the age was actually reset, not just deduplicated.
	expected.Tick(Graphite::ExpectedWrites::kExpectedWriteSeconds - 0.1f);

	CHECK(expected.TakeFor("D:\\repo\\a.txt"));
}
