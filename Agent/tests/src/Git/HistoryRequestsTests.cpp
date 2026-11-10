#include <doctest/doctest.h>

#include "Git/GitRequests.h"

#include "Core/Remote/GitCodec.h"

#include "GitFixture.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

	Core::Result<Agent::GitAnswer> Call(const Agent::GitHandlers& handlers, const std::string& operation, json arguments)
	{
		const auto handler = handlers.find(operation);
		REQUIRE(handler != handlers.end());
		return handler->second(Core::AgentRequest{.Id = 1, .Operation = operation, .Arguments = std::move(arguments)});
	}

	json At(const GitFixture& fixture, json more = json::object())
	{
		more["root"] = fixture.GetPath().string(); // not a path conversion: Linux paths are their bytes
		more["reach"] = "up";
		return more;
	}

	json PayloadOf(const Core::Result<Agent::GitAnswer>& answer)
	{
		REQUIRE_MESSAGE(answer.HasValue(), (answer ? "" : answer.GetError().Message));
		return json::parse(answer.GetValue().Payload);
	}

	std::vector<std::string> BranchNames(const std::vector<Core::Branch>& branches)
	{
		std::vector<std::string> names;

		for (const Core::Branch& branch : branches)
		{
			names.push_back(branch.Name);
		}

		std::sort(names.begin(), names.end());
		return names;
	}

	std::vector<std::string> BranchNamesOf(const Agent::GitHandlers& handlers, const GitFixture& fixture)
	{
		const Core::Result<std::vector<Core::Branch>> branches = Core::BranchesFromJson(PayloadOf(Call(handlers, "git.branches", At(fixture))));
		REQUIRE(branches.HasValue());
		return BranchNames(branches.GetValue());
	}
}

TEST_CASE("Branches and the log of a repository come from the agent as the host's port reads them")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string second = fixture.Commit("second");
	fixture.Branch("feature");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<std::vector<Core::Branch>> branches = Core::BranchesFromJson(PayloadOf(Call(handlers, "git.branches", At(fixture))));
	REQUIRE(branches.HasValue());
	CHECK(BranchNames(branches.GetValue()).size() == 2);
	CHECK(std::all_of(branches.GetValue().begin(), branches.GetValue().end(), [&](const Core::Branch& branch) { return branch.Tip.ToHex() == second; }));

	Core::LogRequest request;
	request.MaxCount = 1;
	const Core::Result<Core::LogPage> page = Core::LogPageFromJson(PayloadOf(Call(handlers, "git.log", At(fixture, {{"request", Core::ToJson(request)}}))));
	REQUIRE(page.HasValue());
	REQUIRE(page.GetValue().Commits.size() == 1);
	CHECK(page.GetValue().Commits[0].Id.ToHex() == second);
	CHECK(page.GetValue().Commits[0].Summary == "second");
	CHECK_FALSE(page.GetValue().Complete);
}

TEST_CASE("Counting commits of a known tip answers without counting")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string tip = fixture.Commit("second");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> counted = Call(handlers, "git.countCommits", At(fixture, {{"firstParent", false}}));
	REQUIRE(counted.HasValue());
	CHECK(counted.GetValue().Result.at("tip") == tip);
	CHECK(counted.GetValue().Result.at("count") == 2);

	const Core::Result<Agent::GitAnswer> known =
		Call(handlers, "git.countCommits", At(fixture, {{"ref", "refs/heads/master"}, {"firstParent", true}, {"knownTip", tip}}));
	REQUIRE(known.HasValue());
	CHECK(known.GetValue().Result.at("tip") == tip);
	CHECK_FALSE(known.GetValue().Result.contains("count"));
}

TEST_CASE("A commit's sides are its file and its first parent's")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const std::string second = fixture.Commit("second");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> sides =
		Call(handlers, "git.commitSides", At(fixture, {{"commit", second}, {"oldPath", "a.txt"}, {"newPath", "a.txt"}}));

	REQUIRE(sides.HasValue());
	const json& listed = sides.GetValue().Result.at("sides");
	REQUIRE(listed.size() == 2);
	CHECK(listed[0].at("present") == true);
	CHECK(listed[1].at("present") == true);
	CHECK(sides.GetValue().Payload == "two\none\n");
}

TEST_CASE("A root commit's old side is absent")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> sides =
		Call(handlers, "git.commitSides", At(fixture, {{"commit", first}, {"oldPath", "a.txt"}, {"newPath", "a.txt"}}));

	REQUIRE(sides.HasValue());
	CHECK(sides.GetValue().Result.at("sides")[1].at("present") == false);
	CHECK(sides.GetValue().Payload == "one\n");
}

TEST_CASE("A stash saved, listed and popped through the agent")
{
	GitFixture fixture;
	fixture.SetIdentity("Tester", "tester@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> saved = Call(handlers, "git.stashSave", At(fixture, {{"message", "keep"}, {"untracked", false}}));
	REQUIRE_MESSAGE(saved.HasValue(), (saved ? "" : saved.GetError().Message));
	CHECK(saved.GetValue().Result.at("saved") == true);

	const Core::Result<std::vector<Core::StashEntry>> stashes = Core::StashesFromJson(PayloadOf(Call(handlers, "git.stashes", At(fixture))));
	REQUIRE(stashes.HasValue());
	REQUIRE(stashes.GetValue().size() == 1);
	CHECK(stashes.GetValue()[0].Message.find("keep") != std::string::npos);

	REQUIRE(Call(handlers, "git.stashPop", At(fixture, {{"id", stashes.GetValue()[0].Id.ToHex()}})).HasValue());
	CHECK(Core::StashesFromJson(PayloadOf(Call(handlers, "git.stashes", At(fixture)))).GetValue().empty());
}

TEST_CASE("A checkout of another branch names the files it changed")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Branch("feature");
	fixture.Checkout("feature");
	fixture.Write("a.txt", "feature\n");
	fixture.Commit("on feature");
	fixture.Checkout("master");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const json target = Core::ToJson(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = "feature"});

	const Core::Result<Core::CheckoutPlan> plan = Core::CheckoutPlanFromJson(PayloadOf(Call(handlers, "git.planCheckout", At(fixture, {{"target", target}}))));
	REQUIRE(plan.HasValue());
	CHECK(plan.GetValue().Overwritten.empty());

	const Core::Result<Core::CheckoutOutcome> outcome = Core::CheckoutOutcomeFromJson(PayloadOf(Call(handlers, "git.checkout", At(fixture, {{"target", target}}))));
	REQUIRE(outcome.HasValue());
	CHECK_FALSE(outcome.GetValue().Failure.has_value());
	REQUIRE(outcome.GetValue().Changed.size() == 1);
	CHECK(outcome.GetValue().Changed[0] == std::filesystem::path{"a.txt"});
}

TEST_CASE("Creating, renaming and deleting a branch through the agent")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	REQUIRE(Call(handlers, "git.createBranch", At(fixture, {{"name", "topic"}, {"from", first}})).HasValue());
	CHECK(BranchNamesOf(handlers, fixture) == std::vector<std::string>{"master", "topic"});

	REQUIRE(Call(handlers, "git.renameBranch", At(fixture, {{"from", "topic"}, {"to", "renamed"}})).HasValue());
	CHECK(BranchNamesOf(handlers, fixture) == std::vector<std::string>{"master", "renamed"});

	const Core::Result<Agent::GitAnswer> unmerged = Call(handlers, "git.countUnmerged", At(fixture, {{"name", "renamed"}}));
	REQUIRE(unmerged.HasValue());
	CHECK(unmerged.GetValue().Result.at("count") == 0);

	REQUIRE(Call(handlers, "git.deleteBranch", At(fixture, {{"name", "renamed"}})).HasValue());
	CHECK(BranchNamesOf(handlers, fixture) == std::vector<std::string>{"master"});
}

TEST_CASE("Tags, remotes and the user's email come from the agent")
{
	GitFixture fixture;
	fixture.SetIdentity("Tester", "tester@example.com");
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.Commit("first");
	fixture.TagLightweight("v1", first);
	fixture.AddRemoteWithUrl("origin", "https://example.com/r.git");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<std::vector<Core::Tag>> tags = Core::TagsFromJson(PayloadOf(Call(handlers, "git.tags", At(fixture))));
	REQUIRE(tags.HasValue());
	REQUIRE(tags.GetValue().size() == 1);
	CHECK(tags.GetValue()[0].Name == "v1");

	const Core::Result<std::vector<Core::Remote>> remotes = Core::RemotesFromJson(PayloadOf(Call(handlers, "git.remotes", At(fixture))));
	REQUIRE(remotes.HasValue());
	REQUIRE(remotes.GetValue().size() == 1);
	CHECK(remotes.GetValue()[0].Url == "https://example.com/r.git");

	const Core::Result<Agent::GitAnswer> email = Call(handlers, "git.userEmail", At(fixture));
	REQUIRE(email.HasValue());
	CHECK(email.GetValue().Result.at("email") == "tester@example.com");
}

TEST_CASE("Own authors of a tip come from the agent")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	const std::string first = fixture.CommitAs("first", "Anna", "anna@example.com");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const Core::OwnAuthorsRequest request{.Tips = {*Core::Oid::FromHex(first)}};

	const Core::Result<std::vector<Core::TipAuthors>> authors =
		Core::TipAuthorsFromJson(PayloadOf(Call(handlers, "git.ownAuthors", At(fixture, {{"request", Core::ToJson(request)}}))));

	REQUIRE(authors.HasValue());
	REQUIRE(authors.GetValue().size() == 1);
	REQUIRE(authors.GetValue()[0].Authors.size() == 1);
	CHECK(authors.GetValue()[0].Authors[0].Email == "anna@example.com");
}

TEST_CASE("The common directory is the repository's .git")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> common = Call(handlers, "git.commonDirectory", At(fixture));

	REQUIRE(common.HasValue());
	CHECK(std::filesystem::path{common.GetValue().Result.at("path").get<std::string>()} ==
		  std::filesystem::weakly_canonical(fixture.GetPath() / ".git"));
}
