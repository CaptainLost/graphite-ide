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
		std::erase(content, '\r');
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

TEST_CASE("Stashing untracked files keeps ignored ones, even inside an untracked directory")
{
	GitFixture fixture;
	fixture.SetIdentity("Tester", "tester@example.com");
	fixture.Write(".gitignore", "*.log\n");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("scratch/note.txt", "untracked\n");
	fixture.Write("scratch/build.log", "ignored inside an untracked directory\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	auto saved = repository.StashSave("with untracked", true);
	REQUIRE(saved.HasValue());
	CHECK(saved.GetValue());

	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "scratch" / "note.txt"));
	REQUIRE(std::filesystem::exists(fixture.GetPath() / "scratch" / "build.log"));
	CHECK(Read(fixture.GetPath() / "scratch" / "build.log") == "ignored inside an untracked directory\n");

	const Core::Oid id = repository.QueryStashes().GetValue()[0].Id;
	REQUIRE(repository.StashApply(id).HasValue());
	CHECK(Read(fixture.GetPath() / "scratch" / "note.txt") == "untracked\n");
}

TEST_CASE("Stashing over unresolved conflicts fails with the reason and changes nothing")
{
	GitFixture fixture;
	Prepare(fixture);
	fixture.Branch("side");
	fixture.Write("a.txt", "ours\n");
	fixture.Commit("ours");
	fixture.Checkout("side");
	fixture.Write("a.txt", "theirs\n");
	fixture.Commit("theirs");
	fixture.Checkout("master");
	fixture.StartConflict("side");

	Core::Repository repository = OpenAt(fixture.GetPath());
	const std::string before = Read(fixture.GetPath() / "a.txt");

	auto saved = repository.StashSave("over a conflict", false);
	REQUIRE_FALSE(saved.HasValue());
	CHECK(saved.GetError().Message.find("resolve or discard the conflicts first") != std::string::npos);
	CHECK(Read(fixture.GetPath() / "a.txt") == before);
	CHECK(repository.QueryStashes().GetValue().empty());
}

TEST_CASE("Popping a stash that conflicts with the branch keeps the stash and says so")
{
	GitFixture fixture;
	Prepare(fixture);
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("a.txt", "feature\n");
	fixture.Commit("feature");
	fixture.Checkout("master");
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenAt(fixture.GetPath());
	REQUIRE(repository.StashSave("on master", false).HasValue());
	const Core::Oid id = repository.QueryStashes().GetValue()[0].Id;
	REQUIRE_FALSE(repository.Checkout({Core::CheckoutKind::LocalBranch, "feature", {}}).Failure.has_value());

	// libgit2 applies a conflicting stash with the conflict in the index and reports
	// success; git keeps the stash then, and so must a pop.
	auto popped = repository.StashPop(id);

	REQUIRE_FALSE(popped.HasValue());
	CHECK(popped.GetError().Message.find("kept as stash@{0}") != std::string::npos);
	CHECK(repository.QueryStashes().GetValue().size() == 1);

	auto status = repository.QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries[0].Path == std::filesystem::path{"a.txt"});
	CHECK(status.GetValue().Entries[0].IsConflicted);
}

TEST_CASE("Stashing ends a merge, a cherry-pick or a revert, as git does, but not a rebase or a bisect")
{
	for (const bool includeUntracked : {false, true})
	{
		CAPTURE(includeUntracked);

		GitFixture fixture;
		Prepare(fixture);
		const std::string head = fixture.Commit("two");
		fixture.Write(".git/rebase-merge/head-name", "refs/heads/master\n");
		fixture.Write(".git/BISECT_LOG", "\n");
		fixture.Write(".git/MERGE_HEAD", head + "\n");
		fixture.Write(".git/MERGE_MSG", "\n");
		fixture.Write("a.txt", "changed\n");
		fixture.Write("new.txt", "fresh\n");

		Core::Repository repository = OpenAt(fixture.GetPath());

		auto saved = repository.StashSave("mid-rebase", includeUntracked);
		REQUIRE(saved.HasValue());
		CHECK(saved.GetValue());

		CHECK(Read(fixture.GetPath() / "a.txt") == "one\n");
		CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt") != includeUntracked);
		CHECK(std::filesystem::exists(fixture.GetPath() / ".git" / "rebase-merge" / "head-name"));
		CHECK(std::filesystem::exists(fixture.GetPath() / ".git" / "BISECT_LOG"));
		CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / ".git" / "MERGE_HEAD"));
		CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / ".git" / "MERGE_MSG"));
	}
}
