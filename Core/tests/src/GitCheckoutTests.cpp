#include <doctest/doctest.h>

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <algorithm>
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

	bool Contains(const std::vector<std::filesystem::path>& paths, const char* path)
	{
		return std::find(paths.begin(), paths.end(), std::filesystem::path{path}) != paths.end();
	}

	bool HasLocalBranch(const Core::Repository& repository, const std::string& name)
	{
		auto branches = repository.QueryBranches();
		REQUIRE(branches.HasValue());

		return std::any_of(branches.GetValue().begin(), branches.GetValue().end(),
						   [&](const Core::Branch& branch) {
							   return branch.Kind == Core::RefKind::Local && branch.Name == name;
						   });
	}

	Core::CheckoutTarget Local(const std::string& name)
	{
		return {.Kind = Core::CheckoutKind::LocalBranch, .Name = name};
	}

	// master: a.txt "one", b.txt "same"; feature: a.txt "feature", b.txt "same".
	void TwoBranches(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		fixture.Write("b.txt", "same\n");
		fixture.Commit("base");
		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Write("a.txt", "feature\n");
		fixture.Commit("feature change");
		fixture.Checkout("master");
	}

	// master: a.txt and gone.txt; feature: a.txt and added.txt.
	void AddAndRemove(GitFixture& fixture)
	{
		fixture.Write("a.txt", "one\n");
		fixture.Write("gone.txt", "only on master\n");
		fixture.Commit("base");
		fixture.Branch("feature");
		fixture.Checkout("feature");
		fixture.Remove("gone.txt");
		fixture.Write("added.txt", "only on feature\n");
		fixture.Commit("add one, remove one");
		fixture.Checkout("master");
	}
}

TEST_CASE("A clean tree plans no overwrite and lists the files the checkout would change")
{
	GitFixture fixture;
	TwoBranches(fixture);

	const Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("feature"));

	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(Contains(plan.GetValue().Updated, "a.txt"));
	CHECK_FALSE(Contains(plan.GetValue().Updated, "b.txt"));
}

TEST_CASE("A plan lists the files the checkout would create and the ones it would remove")
{
	GitFixture fixture;
	AddAndRemove(fixture);

	const Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("feature"));

	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(Contains(plan.GetValue().Updated, "added.txt"));
	CHECK(Contains(plan.GetValue().Updated, "gone.txt"));
	CHECK_FALSE(Contains(plan.GetValue().Updated, "a.txt"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "gone.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "added.txt"));
}

TEST_CASE("A change to a file the branches differ in is planned as overwritten, and planning touches nothing")
{
	GitFixture fixture;
	TwoBranches(fixture);
	fixture.Write("a.txt", "my edit\n");
	fixture.Write("b.txt", "my other edit\n");

	const Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("feature"));

	REQUIRE(plan.HasValue());
	CHECK(Contains(plan.GetValue().Overwritten, "a.txt"));
	CHECK_FALSE(Contains(plan.GetValue().Overwritten, "b.txt"));
	CHECK(Read(fixture.GetPath() / "a.txt") == "my edit\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("A change to a file the target removes is planned as overwritten, not as removed")
{
	GitFixture fixture;
	AddAndRemove(fixture);
	fixture.Write("gone.txt", "my edit\n");

	const Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("feature"));

	REQUIRE(plan.HasValue());
	CHECK(Contains(plan.GetValue().Overwritten, "gone.txt"));
	CHECK_FALSE(Contains(plan.GetValue().Updated, "gone.txt"));
}

TEST_CASE("Checkout switches branch, carries changes the branches do not differ in, and reports what it changed")
{
	GitFixture fixture;
	TwoBranches(fixture);
	fixture.Write("b.txt", "carried\n");

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::CheckoutOutcome changed = repository.Checkout(Local("feature"));

	REQUIRE_FALSE(changed.Failure.has_value());
	CHECK(Contains(changed.Changed, "a.txt"));
	CHECK(repository.QueryHead().GetValue().BranchName == "feature");
	CHECK(Read(fixture.GetPath() / "a.txt") == "feature\n");
	CHECK(Read(fixture.GetPath() / "b.txt") == "carried\n");
}

TEST_CASE("Checkout reports the files it created and the ones it removed")
{
	GitFixture fixture;
	AddAndRemove(fixture);

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::CheckoutOutcome changed = repository.Checkout(Local("feature"));

	REQUIRE_FALSE(changed.Failure.has_value());
	CHECK(Contains(changed.Changed, "added.txt"));
	CHECK(Contains(changed.Changed, "gone.txt"));
	CHECK_FALSE(Contains(changed.Changed, "a.txt"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "added.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "gone.txt"));
}

TEST_CASE("Checkout refuses to overwrite a change and leaves everything as it was")
{
	GitFixture fixture;
	TwoBranches(fixture);
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	CHECK(repository.Checkout(Local("feature")).Failure.has_value());
	CHECK(Read(fixture.GetPath() / "a.txt") == "my edit\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("A refused checkout reports the failure and names no changed path")
{
	GitFixture fixture;
	AddAndRemove(fixture);
	fixture.Write("gone.txt", "my edit\n");

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::CheckoutOutcome outcome = repository.Checkout(Local("feature"));

	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Code == Core::ErrorCode::InvalidArgument);
	CHECK(outcome.Changed.empty());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "added.txt"));
}

TEST_CASE("Checkout refused over one file writes and removes none of the others")
{
	GitFixture fixture;
	AddAndRemove(fixture);
	fixture.Write("added.txt", "my untracked work\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	CHECK(repository.Checkout(Local("feature")).Failure.has_value());
	CHECK(Read(fixture.GetPath() / "added.txt") == "my untracked work\n");
	CHECK(Read(fixture.GetPath() / "gone.txt") == "only on master\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("Checking out a commit detaches HEAD there")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("one");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::Oid target = repository.QueryLog({}).GetValue().Commits.back().Id;

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::Commit, .Commit = target}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.Kind == Core::HeadKind::Detached);
	CHECK(head.Commit->ToHex() == first);
	CHECK(Read(fixture.GetPath() / "a.txt") == "one\n");
}

TEST_CASE("Checking out a remote branch creates a local branch tracking it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "remote\n");
	const std::string remoteTip = fixture.Commit("remote work");
	fixture.PushTo("topic", remoteTip);

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.BranchName == "topic");

	auto branches = repository.QueryBranches().GetValue();
	const auto topic = std::find_if(branches.begin(), branches.end(), [](const Core::Branch& branch) {
		return branch.Kind == Core::RefKind::Local && branch.Name == "topic";
	});
	REQUIRE(topic != branches.end());
	REQUIRE(topic->Upstream.has_value());
	CHECK(*topic->Upstream == "origin/topic");
}

TEST_CASE("A refused checkout of a remote branch leaves no local branch behind")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string remoteTip = fixture.Commit("one");
	fixture.Write("a.txt", "two\n");
	fixture.Commit("two");
	fixture.PushTo("topic", remoteTip);
	fixture.Write("a.txt", "my edit\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	CHECK(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());
	CHECK_FALSE(HasLocalBranch(repository, "topic"));
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
	CHECK(Read(fixture.GetPath() / "a.txt") == "my edit\n");
}

TEST_CASE("Checking out a remote branch whose local namesake exists switches to it without moving it")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string base = fixture.Commit("one");
	fixture.Branch("topic");
	fixture.Write("a.txt", "remote\n");
	const std::string remoteTip = fixture.Commit("remote work");
	fixture.PushTo("topic", remoteTip);

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE_FALSE(repository.Checkout({.Kind = Core::CheckoutKind::RemoteBranch, .Name = "origin/topic"}).Failure.has_value());

	const Core::HeadState head = repository.QueryHead().GetValue();
	CHECK(head.BranchName == "topic");
	CHECK(head.Commit->ToHex() == base);
}

TEST_CASE("Checking out a branch that does not exist is NotFound")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	Core::Repository repository = OpenAt(fixture.GetPath());

	const Core::CheckoutOutcome changed = repository.Checkout(Local("nowhere"));
	REQUIRE(changed.Failure.has_value());
	CHECK(changed.Failure->Code == Core::ErrorCode::NotFound);
	CHECK(changed.Changed.empty());
}

TEST_CASE("Discarding everything resets tracked changes and removes untracked files only when asked")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("a.txt", "edit\n");
	fixture.Write("new.txt", "fresh\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(false).HasValue());
	CHECK(Read(fixture.GetPath() / "a.txt") == "one\n");
	CHECK(std::filesystem::exists(fixture.GetPath() / "new.txt"));

	REQUIRE(repository.DiscardAll(true).HasValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "new.txt"));
}

TEST_CASE("Discarding untracked files leaves ignored ones, even inside an untracked directory")
{
	GitFixture fixture;
	fixture.Write(".gitignore", "*.log\n");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("top.log", "ignored\n");
	fixture.Write("scratch/note.txt", "untracked\n");
	fixture.Write("scratch/build.log", "ignored inside an untracked directory\n");
	fixture.Write("drafts/deep/only.txt", "untracked\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(true).HasValue());
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "scratch" / "note.txt"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetPath() / "drafts"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "top.log"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "scratch" / "build.log"));
}

TEST_CASE("Discarding untracked files leaves an untracked nested repository alone")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Write("sub/file", "inside a repository of its own\n");

	git_repository* nested = nullptr;
	REQUIRE(git_repository_init(&nested, Core::ToUtf8(fixture.GetPath() / "sub").c_str(), 0) == 0);
	git_repository_free(nested);

	Core::Repository repository = OpenAt(fixture.GetPath());

	REQUIRE(repository.DiscardAll(true).HasValue());
	CHECK(std::filesystem::exists(fixture.GetPath() / "sub" / ".git"));
	CHECK(std::filesystem::exists(fixture.GetPath() / "sub" / "file"));
}

TEST_CASE("With no commit yet, planning, checking out and discarding fail as NotFound and touch nothing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "not committed\n");

	Core::Repository repository = OpenAt(fixture.GetPath());

	auto plan = repository.PlanCheckout(Local("master"));
	REQUIRE_FALSE(plan.HasValue());
	CHECK(plan.GetError().Code == Core::ErrorCode::NotFound);

	const Core::CheckoutOutcome outcome = repository.Checkout(Local("master"));
	REQUIRE(outcome.Failure.has_value());
	CHECK(outcome.Failure->Code == Core::ErrorCode::NotFound);
	CHECK(outcome.Changed.empty());

	auto discarded = repository.DiscardAll(true);
	REQUIRE_FALSE(discarded.HasValue());
	CHECK(discarded.GetError().Code == Core::ErrorCode::NotFound);

	CHECK(Read(fixture.GetPath() / "a.txt") == "not committed\n");
}

TEST_CASE("From an unborn branch, a checkout writes the target's files and removes nothing")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");

	// An orphan branch: HEAD names a branch with no commit, while master has one.
	git_repository* handle = nullptr;
	REQUIRE(git_repository_open(&handle, Core::ToUtf8(fixture.GetPath()).c_str()) == 0);
	REQUIRE(git_repository_set_head(handle, "refs/heads/orphan") == 0);
	git_repository_free(handle);
	fixture.Remove("a.txt");

	Core::Repository repository = OpenAt(fixture.GetPath());

	auto plan = repository.PlanCheckout(Local("master"));
	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());
	CHECK(Contains(plan.GetValue().Updated, "a.txt"));

	const Core::CheckoutOutcome outcome = repository.Checkout(Local("master"));
	REQUIRE_FALSE(outcome.Failure.has_value());
	CHECK(outcome.Changed == std::vector<std::filesystem::path>{"a.txt"});
	CHECK(Read(fixture.GetPath() / "a.txt") == "one\n");
	CHECK(repository.QueryHead().GetValue().BranchName == "master");
}

TEST_CASE("An untracked file the target tracks is planned as overwritten")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("one");
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("notes.txt", "tracked on feature\n");
	fixture.Commit("add notes");
	fixture.Checkout("master");
	fixture.Write("notes.txt", "my untracked notes\n");

	const Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("feature"));

	REQUIRE(plan.HasValue());
	CHECK(Contains(plan.GetValue().Overwritten, "notes.txt"));
}

namespace {

	// master and side both changed a.txt; master is mid-merge of side with a.txt conflicted.
	// "elsewhere" is a branch at the base that also adds c.txt.
	void ConflictedMerge(GitFixture& fixture)
	{
		fixture.Write("a.txt", "base\n");
		fixture.Write("b.txt", "same\n");
		fixture.Commit("base");
		fixture.Branch("side");
		fixture.Branch("elsewhere");
		fixture.Write("a.txt", "ours\n");
		fixture.Commit("ours");
		fixture.Checkout("side");
		fixture.Write("a.txt", "theirs\n");
		fixture.Commit("theirs");
		fixture.Checkout("elsewhere");
		fixture.Write("c.txt", "only elsewhere\n");
		fixture.Commit("elsewhere");
		fixture.Checkout("master");
		fixture.StartConflict("side");
	}
}

TEST_CASE("Unresolved conflicts in the index are planned as overwritten, beside what the checkout would write")
{
	GitFixture fixture;
	ConflictedMerge(fixture);

	Core::Repository repository = OpenAt(fixture.GetPath());
	auto plan = repository.PlanCheckout(Local("elsewhere"));

	REQUIRE(plan.HasValue());
	CHECK(Contains(plan.GetValue().Overwritten, "a.txt"));
	CHECK(Contains(plan.GetValue().Updated, "c.txt"));
	CHECK_FALSE(Contains(plan.GetValue().Updated, "a.txt"));
}

TEST_CASE("Checkout over unresolved conflicts is refused as such, and goes ahead once they are discarded")
{
	GitFixture fixture;
	ConflictedMerge(fixture);

	Core::Repository repository = OpenAt(fixture.GetPath());
	const Core::CheckoutOutcome refused = repository.Checkout(Local("elsewhere"));

	REQUIRE(refused.Failure.has_value());
	CHECK(refused.Failure->Code == Core::ErrorCode::InvalidArgument);
	CHECK(refused.Failure->Message.find("resolve or discard the conflicts first") != std::string::npos);
	CHECK(refused.Changed.empty());
	CHECK(repository.QueryHead().GetValue().BranchName == "master");

	REQUIRE(repository.DiscardAll(false).HasValue());

	const Core::CheckoutOutcome outcome = repository.Checkout(Local("elsewhere"));
	CHECK_FALSE(outcome.Failure.has_value());
	CHECK(repository.QueryHead().GetValue().BranchName == "elsewhere");
	CHECK(Read(fixture.GetPath() / "c.txt") == "only elsewhere\n");
}
