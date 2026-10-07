#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <filesystem>

using Core::Repository;

TEST_CASE("Init makes a folder a repository that Open then finds")
{
	TempDirectory directory;
	directory.MakeFile("a.txt", "one\n");

	CHECK_FALSE(Repository::Open(directory.GetPath()).HasValue());

	const Core::Result<Repository> created = Repository::Init(directory.GetPath());
	REQUIRE(created.HasValue());

	CHECK(std::filesystem::is_directory(directory.GetPath() / ".git"));

	const Core::Result<Repository> opened = Repository::Open(directory.GetPath());
	REQUIRE(opened.HasValue());

	const Core::Result<Core::HeadState> head = opened.GetValue().QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);

	// The branch is named by the machine's init.defaultBranch, so only its presence is portable.
	CHECK_FALSE(head.GetValue().BranchName.empty());
}

TEST_CASE("Init leaves the files already in the folder alone")
{
	TempDirectory directory;
	directory.MakeFile("a.txt", "one\n");

	REQUIRE(Repository::Init(directory.GetPath()).HasValue());

	const Core::Result<Core::WorkingTreeStatus> status = Repository::Open(directory.GetPath()).GetValue().QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries.front().IsUntracked);
}

TEST_CASE("Init refuses a folder that does not exist rather than creating it")
{
	TempDirectory directory;

	CHECK_FALSE(Repository::Init(directory.GetPath() / "missing").HasValue());
	CHECK_FALSE(std::filesystem::exists(directory.GetPath() / "missing"));
}
