#include <doctest/doctest.h>

#include "Core/Git/Libgit2Support.h"

#include <git2.h>

TEST_CASE("An ownership refusal is access denied and says whose files they are")
{
	const Core::Error failure =
		Core::RepositoryOpenFailure(GIT_EOWNER, "D:/x", "repository path 'D:/x/' is not owned by current user");

	CHECK(failure.Code == Core::ErrorCode::AccessDenied);
	CHECK(failure.Message == "git refuses to open the repository at 'D:/x' because its files belong to another user");
}

TEST_CASE("An ownership refusal inside WSL also says WSL folders are not supported yet")
{
	const Core::Error failure = Core::RepositoryOpenFailure(GIT_EOWNER, "\\\\wsl.localhost\\Ubuntu\\p", "not owned");

	CHECK(failure.Code == Core::ErrorCode::AccessDenied);
	CHECK(failure.Message.ends_with("; folders inside WSL are not supported yet"));
}

TEST_CASE("A missing repository stays not found, and other failures stay IO failures")
{
	CHECK(Core::RepositoryOpenFailure(GIT_ENOTFOUND, "D:/x", "m").Code == Core::ErrorCode::NotFound);
	CHECK(Core::RepositoryOpenFailure(GIT_EAUTH, "D:/x", "m").Code == Core::ErrorCode::AccessDenied);

	const Core::Error other = Core::RepositoryOpenFailure(GIT_ERROR, "D:/x", "m");

	CHECK(other.Code == Core::ErrorCode::IoFailure);
	CHECK(other.Message == "cannot open a git repository at 'D:/x': m");
}
