#include <doctest/doctest.h>

#include <git2.h>

TEST_CASE("libgit2 links and reports its version")
{
	// The point is not the numbers but that the library compiled, linked and can be
	// called at all — everything else in this integration builds on that.
	int major = 0;
	int minor = 0;
	int revision = 0;

	git_libgit2_version(&major, &minor, &revision);

	CHECK(major == 1);
	CHECK(minor == 8);
}

TEST_CASE("libgit2 initialises and shuts down cleanly")
{
	CHECK(git_libgit2_init() >= 1);
	CHECK(git_libgit2_shutdown() >= 0);
}
