#include <doctest/doctest.h>

#include "Git/GitRequests.h"

#include "Core/Remote/GitCodec.h"

#include "GitFixture.h"

#include <string>

using nlohmann::json;

namespace {

	Core::Result<Agent::GitAnswer> Call(const Agent::GitHandlers& handlers, const std::string& operation, json arguments, std::string payload = {})
	{
		const auto handler = handlers.find(operation);
		REQUIRE(handler != handlers.end());
		return handler->second(Core::AgentRequest{.Id = 1, .Operation = operation, .Arguments = std::move(arguments), .Payload = std::move(payload)});
	}

	json At(const GitFixture& fixture, json more = json::object())
	{
		more["root"] = fixture.GetPath().string(); // not a path conversion: Linux paths are their bytes
		more["reach"] = "up";
		return more;
	}

	// A branch whose one change master took as a squash commit of its own.
	struct SquashedBranch
	{
		SquashedBranch()
		{
			Fixture.SetIdentity("Tester", "tester@example.com");
			Fixture.Write("a.txt", "one\n");
			Base = Fixture.Commit("base");
			Fixture.Branch("feature");
			Fixture.Checkout("feature");
			Fixture.Write("b.txt", "feature\n");
			FeatureTip = Fixture.Commit("feature work");
			Fixture.Checkout("master");
			Fixture.Write("b.txt", "feature\n");
			Squash = Fixture.Commit("squash feature");
		}

		GitFixture Fixture;
		std::string Base;
		std::string FeatureTip;
		std::string Squash;
	};
}

TEST_CASE("A branch squashed into master is merged by its contents, and the check writes nothing into the repository")
{
	SquashedBranch history;
	const std::size_t objectsBefore = history.Fixture.ObjectFileCount();
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer =
		Call(handlers, "git.checkContent", At(history.Fixture, {{"branchTip", history.FeatureTip}, {"lineTip", history.Squash}}));

	REQUIRE_MESSAGE(answer.HasValue(), (answer ? "" : answer.GetError().Message));
	const Core::Result<Core::ContentVerdict> verdict = Core::ContentVerdictFromJson(json::parse(answer.GetValue().Payload));
	REQUIRE(verdict.HasValue());
	CHECK(verdict.GetValue().Outcome == Core::ContentOutcome::Merged);
	CHECK(history.Fixture.ObjectFileCount() == objectsBefore);
}

TEST_CASE("A commit's twin on the line is found by its patch")
{
	SquashedBranch history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer =
		Call(handlers, "git.findTwin", At(history.Fixture, {{"commit", history.FeatureTip}, {"lineTip", history.Squash}, {"budget", 100}}));

	REQUIRE(answer.HasValue());
	const Core::Result<Core::PatchTwinSearch> search = Core::PatchTwinSearchFromJson(json::parse(answer.GetValue().Payload));
	REQUIRE(search.HasValue());
	REQUIRE(search.GetValue().Twin.has_value());
	CHECK(search.GetValue().Twin->ToHex() == history.Squash);
}

TEST_CASE("The paths changed between two commits, and whether trees differ at paths, come from the agent")
{
	SquashedBranch history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> changed = Call(handlers, "git.pathsChangedBetween", At(history.Fixture, {{"older", history.Base}, {"newer", history.Squash}}));
	REQUIRE(changed.HasValue());
	CHECK(json::parse(changed.GetValue().Payload) == json::array({"b.txt"}));

	const Core::Result<Agent::GitAnswer> differ =
		Call(handlers, "git.treesDifferAt", At(history.Fixture, {{"from", history.FeatureTip}, {"to", history.Squash}}), json::array({"b.txt"}).dump());
	REQUIRE(differ.HasValue());
	CHECK(differ.GetValue().Result.at("differ") == false);

	const Core::Result<Agent::GitAnswer> differs =
		Call(handlers, "git.treesDifferAt", At(history.Fixture, {{"from", history.Base}, {"to", history.Squash}}), json::array({"b.txt"}).dump());
	REQUIRE(differs.HasValue());
	CHECK(differs.GetValue().Result.at("differ") == true);
}
