#include <doctest/doctest.h>

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include "Git/GitFixture.h"
#include "Git/WorktreeFixture.h"
#include "TempDirectory.h"

#include <git2.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	std::filesystem::path HoldingPath(const Core::Repository& repository, const std::string& branchFullName)
	{
		const Core::Result<std::optional<std::filesystem::path>> holding = repository.WorktreeHolding(branchFullName);
		REQUIRE(holding.HasValue());
		REQUIRE(holding.GetValue().has_value());
		return *holding.GetValue();
	}

	bool IsHeldNowhere(const Core::Repository& repository, const std::string& branchFullName)
	{
		const Core::Result<std::optional<std::filesystem::path>> holding = repository.WorktreeHolding(branchFullName);
		REQUIRE(holding.HasValue());
		return !holding.GetValue().has_value();
	}
}

TEST_CASE("A linked worktree's ignore rules include the exclude file and config of the repository it belongs to")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Repository worktree = OpenRepositoryAt(worktreePath);
	const std::vector<std::filesystem::path> files = worktree.IgnoreRuleFilesOutside(worktreePath);

	bool namesCommonExclude = false;
	bool namesCommonConfig = false;

	for (const std::filesystem::path& file : files)
	{
		namesCommonExclude = namesCommonExclude || Core::SamePath(file, fixture.GetPath() / ".git" / "info" / "exclude");
		namesCommonConfig = namesCommonConfig || Core::SamePath(file, fixture.GetPath() / ".git" / "config");
	}

	CHECK(namesCommonExclude);
	CHECK(namesCommonConfig);
}

TEST_CASE("The branch checked out here is held by this working directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(std::filesystem::equivalent(HoldingPath(repository, "refs/heads/master"), fixture.GetPath()));
}

TEST_CASE("A branch checked out in a linked worktree is held by that worktree")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(std::filesystem::equivalent(HoldingPath(repository, "refs/heads/side"), worktreePath));
}

TEST_CASE("A branch checked out nowhere is held by no working directory")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(IsHeldNowhere(repository, "refs/heads/side"));
}

TEST_CASE("A worktree with a detached HEAD holds no branch")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);
	DetachHeadOf(worktreePath);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(IsHeldNowhere(repository, "refs/heads/side"));
}

TEST_CASE("A linked worktree's folder opens exactly as that worktree")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Result<Core::Repository> opened = Core::Repository::OpenExactly(worktreePath);

	REQUIRE(opened.HasValue());
	CHECK(std::filesystem::equivalent(opened.GetValue().GetWorkingDirectory(), worktreePath));
}

TEST_CASE("A linked worktree whose folder was deleted by hand holds no branch")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);
	std::filesystem::remove_all(worktreePath);

	Core::Repository repository = OpenRepositoryAt(fixture.GetPath());

	CHECK(IsHeldNowhere(repository, "refs/heads/side"));
}

TEST_CASE("Asked from a linked worktree the main checkout's branch is held by the main checkout")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("side");

	TempDirectory outside;
	const std::filesystem::path worktreePath = outside.GetPath() / "side-tree";
	AddWorktree(fixture, "side", worktreePath);

	Core::Repository fromWorktree = OpenRepositoryAt(worktreePath);

	CHECK(std::filesystem::equivalent(HoldingPath(fromWorktree, "refs/heads/master"), fixture.GetPath()));
	CHECK(std::filesystem::equivalent(HoldingPath(fromWorktree, "refs/heads/side"), worktreePath));
}
