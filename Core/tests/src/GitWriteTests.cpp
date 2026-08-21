#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

	// The status of one path, or a default-constructed FileStatus when git says the path
	// is clean. Written as a helper because every case below asks the same question.
	Core::FileStatus StatusOf(const Core::Repository& repository, const std::string& relative)
	{
		const auto status = repository.QueryStatus();
		REQUIRE(status.HasValue());

		const auto& entries = status.GetValue().Entries;
		const auto found = std::find_if(entries.begin(), entries.end(),
			[&relative](const Core::FileStatus& file) {
				return file.Path == std::filesystem::path{relative};
			});

		return found == entries.end() ? Core::FileStatus{} : *found;
	}

	bool Has(Core::StatusFlags flags, Core::StatusFlags wanted)
	{
		return (flags & wanted) == wanted;
	}
}

TEST_CASE("Stage adds a new file to the index")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("b.txt", "two\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"b.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository.GetValue(), "b.txt");
	CHECK(Has(after.Index, Core::StatusFlags::New));
	CHECK(after.WorkTree == Core::StatusFlags::None);
}

TEST_CASE("Stage records a modified file")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository.GetValue(), "a.txt");
	CHECK(Has(after.Index, Core::StatusFlags::Modified));
	CHECK(after.WorkTree == Core::StatusFlags::None);
}

TEST_CASE("Stage records a deletion through the same call")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Remove("a.txt");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository.GetValue(), "a.txt");
	CHECK(Has(after.Index, Core::StatusFlags::Deleted));
}

TEST_CASE("Unstage puts a staged file back as HEAD has it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());
	REQUIRE(repository.GetValue().Unstage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository.GetValue(), "a.txt");
	CHECK(after.Index == Core::StatusFlags::None);
	CHECK(Has(after.WorkTree, Core::StatusFlags::Modified));
}

TEST_CASE("Unstage removes the entry when HEAD is unborn")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());
	REQUIRE(repository.GetValue().Unstage({"a.txt"}).HasValue());

	const Core::FileStatus after = StatusOf(repository.GetValue(), "a.txt");
	CHECK(after.IsUntracked);
}

TEST_CASE("Staging many paths at once writes the index once")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");
	fixture.Write("b.txt", "two\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt", "b.txt"}).HasValue());

	CHECK(Has(StatusOf(repository.GetValue(), "a.txt").Index, Core::StatusFlags::Modified));
	CHECK(Has(StatusOf(repository.GetValue(), "b.txt").Index, Core::StatusFlags::New));
}

TEST_CASE("DiscardWorkTree restores a modified file from the index")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().DiscardWorkTree({"a.txt"}).HasValue());

	CHECK(StatusOf(repository.GetValue(), "a.txt").WorkTree == Core::StatusFlags::None);
}

TEST_CASE("DiscardWorkTree leaves untracked files alone")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "fresh\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().DiscardWorkTree({"new.txt"}).HasValue());

	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));
	CHECK(StatusOf(repository.GetValue(), "new.txt").IsUntracked);
}

TEST_CASE("DeleteUntracked removes the file from disk")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("new.txt", "fresh\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().DeleteUntracked({"new.txt"}).HasValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("DeleteUntracked reports a path it cannot remove")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	// A directory with a file in it is not removed by std::filesystem::remove, so this is
	// a failure the caller must be told about rather than a silent no-op.
	fixture.Write("folder/inside.txt", "x\n");

	const auto result = repository.GetValue().DeleteUntracked({"folder"});
	CHECK_FALSE(result.HasValue());
}

TEST_CASE("Commit writes the index as a commit the log then finds")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "changed\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const auto committed = repository.GetValue().Commit({.Message = "second"});
	REQUIRE(committed.HasValue());

	const auto page = repository.GetValue().QueryLog({});
	REQUIRE(page.HasValue());
	REQUIRE_FALSE(page.GetValue().Commits.empty());
	CHECK(page.GetValue().Commits.front().Id == committed.GetValue());
	CHECK(StatusOf(repository.GetValue(), "a.txt").Index == Core::StatusFlags::None);
}

TEST_CASE("Commit fails rather than inventing an author it cannot resolve")
{
	GitFixture fixture;
	fixture.SetUnusableIdentity();
	fixture.Write("a.txt", "one\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const auto committed = repository.GetValue().Commit({.Message = "first"});
	CHECK_FALSE(committed.HasValue());
}

TEST_CASE("Commit into an unborn HEAD makes a root commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const auto committed = repository.GetValue().Commit({.Message = "first"});
	REQUIRE(committed.HasValue());

	const auto parent = repository.GetValue().FirstParentOf(committed.GetValue());
	REQUIRE(parent.HasValue());
	CHECK_FALSE(parent.GetValue().has_value());
}

TEST_CASE("Amend replaces HEAD and keeps its parent")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("b.txt", "two\n");
	fixture.Commit("second");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	const auto before = repository.GetValue().QueryLog({});
	REQUIRE(before.HasValue());
	const std::size_t countBefore = before.GetValue().Commits.size();
	const Core::Oid firstParent = before.GetValue().Commits.front().Id;

	const auto amended = repository.GetValue().Commit({.Message = "second, reworded", .Amend = true});
	REQUIRE(amended.HasValue());
	CHECK(amended.GetValue() != firstParent);

	const auto after = repository.GetValue().QueryLog({});
	REQUIRE(after.HasValue());
	CHECK(after.GetValue().Commits.size() == countBefore);
	CHECK(after.GetValue().Commits.front().Id == amended.GetValue());
	CHECK(after.GetValue().Commits.front().Summary == "second, reworded");
}

TEST_CASE("Amend fails when HEAD is unborn")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "one\n");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	const auto amended = repository.GetValue().Commit({.Message = "nothing to amend", .Amend = true});
	CHECK_FALSE(amended.HasValue());
}

TEST_CASE("Staging a conflicted file clears the conflict")
{
	GitFixture fixture;
	fixture.SetIdentity("Test Author", "test@example.com");
	fixture.Write("a.txt", "base\n");
	fixture.Commit("first");
	fixture.Branch("other");
	fixture.Write("a.txt", "mine\n");
	fixture.Commit("mine");
	fixture.Checkout("other");
	fixture.Write("a.txt", "theirs\n");
	fixture.Commit("theirs");

	auto repository = Core::Repository::Open(fixture.GetPath());
	REQUIRE(repository.HasValue());

	fixture.StartConflict("master");
	REQUIRE(StatusOf(repository.GetValue(), "a.txt").IsConflicted);

	fixture.Write("a.txt", "resolved\n");
	REQUIRE(repository.GetValue().Stage({"a.txt"}).HasValue());

	CHECK_FALSE(StatusOf(repository.GetValue(), "a.txt").IsConflicted);
}
