#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "TempDirectory.h"

#include <filesystem>

using Core::Repository;

TEST_CASE("Init makes a folder a repository that Open then finds")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "one\n");

	CHECK_FALSE(Repository::Open(temp.GetPath()).HasValue());

	auto created = Repository::Init(temp.GetPath());
	REQUIRE(created.HasValue());

	CHECK(std::filesystem::is_directory(temp.GetPath() / ".git"));

	auto opened = Repository::Open(temp.GetPath());
	REQUIRE(opened.HasValue());

	auto head = opened.GetValue().QueryHead();
	REQUIRE(head.HasValue());
	CHECK(head.GetValue().Kind == Core::HeadKind::Unborn);

	// Named by the machine's init.defaultBranch, so only its presence is portable.
	CHECK_FALSE(head.GetValue().BranchName.empty());
}

TEST_CASE("Init leaves the files already in the folder alone")
{
	TempDirectory temp;
	temp.MakeFile("a.txt", "one\n");

	REQUIRE(Repository::Init(temp.GetPath()).HasValue());

	auto status = Repository::Open(temp.GetPath()).GetValue().QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries.front().IsUntracked);
}

TEST_CASE("Init refuses a folder that does not exist rather than creating it")
{
	TempDirectory temp;

	CHECK_FALSE(Repository::Init(temp.GetPath() / "missing").HasValue());
	CHECK_FALSE(std::filesystem::exists(temp.GetPath() / "missing"));
}
