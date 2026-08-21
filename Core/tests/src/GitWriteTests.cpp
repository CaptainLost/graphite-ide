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
