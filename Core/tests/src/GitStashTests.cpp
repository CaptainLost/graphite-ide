#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

	Core::Repository OpenAt(const std::filesystem::path& root)
	{
		auto opened = Core::Repository::Open(root);
		REQUIRE(opened.HasValue());
		return std::move(opened).GetValue();
	}

	std::string Read(const std::filesystem::path& path)
	{
		std::ifstream in{path, std::ios::binary};
		std::stringstream text;
		text << in.rdbuf();

		// A checkout writes the working tree's line endings, which core.autocrlf makes CRLF on
		// a developer's machine; what is under test is the content, not that setting.
		std::string content = text.str();
		std::erase(content, '');
		return content;
	}

	void Prepare(GitFixture& fixture)
	{
		fixture.SetIdentity("Tester", "tester@example.com");
		fixture.Write("a.txt", "one\n");
		fixture.Commit("one");
	}
}

TEST_CASE("Stashing takes tracked changes off the tree and lists them")
{
	GitFixture fixture;
	Prepare(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	auto saved = repository.StashSave("work in progress", false);
	REQUIRE(saved.HasValue());
	CHECK(saved.GetValue());

	CHECK(Read(fixture.GetPath() / "a.txt") == "one\n");

	auto stashes = repository.QueryStashes();
	REQUIRE(stashes.HasValue());
	REQUIRE(stashes.GetValue().size() == 1);
	CHECK(stashes.GetValue()[0].Index == 0);
	CHECK(stashes.GetValue()[0].Message.find("work in progress") != std::string::npos);
	CHECK(stashes.GetValue()[0].Time > 0);
}

TEST_CASE("Untracked files are stashed only when asked")
{
	GitFixture fixture;
	Prepare(fixture);
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	// Nothing tracked changed: not an error, just nothing done.
	auto none = repository.StashSave("nothing", false);
	REQUIRE(none.HasValue());
	CHECK_FALSE(none.GetValue());
	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));

	auto some = repository.StashSave("with untracked", true);
	REQUIRE(some.HasValue());
	CHECK(some.GetValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("Apply restores a stash and keeps it, pop restores and removes it")
{
	GitFixture fixture;
	Prepare(fixture);
	fixture.Write("a.txt", "changed\n");

	Core::Repository repository = OpenAt(fixture.GetPath());
	REQUIRE(repository.StashSave("one", false).HasValue());
	const Core::Oid id = repository.QueryStashes().GetValue()[0].Id;

	REQUIRE(repository.StashApply(id).HasValue());
	CHECK(Read(fixture.GetPath() / "a.txt") == "changed\n");
	CHECK(repository.QueryStashes().GetValue().size() == 1);

	fixture.Write("a.txt", "one\n");

	REQUIRE(repository.StashPop(id).HasValue());
	CHECK(Read(fixture.GetPath() / "a.txt") == "changed\n");
	CHECK(repository.QueryStashes().GetValue().empty());
}

TEST_CASE("A stash is addressed by id, so a stash pushed meanwhile does not shift the target")
{
	GitFixture fixture;
	Prepare(fixture);

	Core::Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("a.txt", "first\n");
	REQUIRE(repository.StashSave("first", false).HasValue());
	const Core::Oid first = repository.QueryStashes().GetValue()[0].Id;

	fixture.Write("a.txt", "second\n");
	REQUIRE(repository.StashSave("second", false).HasValue());

	REQUIRE(repository.StashDrop(first).HasValue());

	auto left = repository.QueryStashes().GetValue();
	REQUIRE(left.size() == 1);
	CHECK(left[0].Message.find("second") != std::string::npos);

	CHECK(repository.StashDrop(first).GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Popping onto a conflicting change fails and keeps the stash")
{
	GitFixture fixture;
	Prepare(fixture);

	Core::Repository repository = OpenAt(fixture.GetPath());

	fixture.Write("a.txt", "stashed\n");
	REQUIRE(repository.StashSave("s", false).HasValue());
	const Core::Oid id = repository.QueryStashes().GetValue()[0].Id;

	fixture.Write("a.txt", "local edit\n");

	CHECK_FALSE(repository.StashPop(id).HasValue());
	CHECK(repository.QueryStashes().GetValue().size() == 1);
	CHECK(Read(fixture.GetPath() / "a.txt") == "local edit\n");
}
