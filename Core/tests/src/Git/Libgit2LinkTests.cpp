#include <doctest/doctest.h>

#include <git2.h>

namespace {

	constexpr int kExpectedMajorVersion = 1;
	constexpr int kExpectedMinorVersion = 8;
}

TEST_CASE("libgit2 links and reports its version")
{
	int majorVersion = 0;
	int minorVersion = 0;
	int revisionVersion = 0;

	git_libgit2_version(&majorVersion, &minorVersion, &revisionVersion);

	CHECK(majorVersion == kExpectedMajorVersion);
	CHECK(minorVersion == kExpectedMinorVersion);
}

TEST_CASE("libgit2 initialises and shuts down cleanly")
{
	CHECK(git_libgit2_init() >= 1);
	CHECK(git_libgit2_shutdown() >= 0);
}
