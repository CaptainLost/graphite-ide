#include <doctest/doctest.h>

#include "Git/GitRequests.h"

#include "Core/Remote/GitCodec.h"

#include "Core/Git/Repository.h"

#include "GitFixture.h"

#include <filesystem>
#include <fstream>
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

	json At(const std::filesystem::path& root, json more = json::object(), const char* reach = "up")
	{
		more["root"] = root.string(); // not a path conversion: Linux paths are their bytes
		more["reach"] = reach;
		return more;
	}

	std::string Linux(const std::filesystem::path& path)
	{
		return path.string(); // not a path conversion
	}

	// Two commits on master changing a.txt, and a branch feature with one more.
	struct ReviewHistory
	{
		ReviewHistory()
		{
			Fixture.SetIdentity("Tester", "tester@example.com");
			Fixture.Write(".gitignore", "*.log\n");
			Fixture.Write("a.txt", "one\n");
			First = Fixture.Commit("first");
			Fixture.Write("a.txt", "one\ntwo\n");
			Second = Fixture.Commit("second");
			Fixture.Branch("feature");
			Fixture.Checkout("feature");
			Fixture.Write("b.txt", "feature\n");
			OnFeature = Fixture.Commit("on feature");
			Fixture.Checkout("master");
		}

		// The blob a.txt had before the second commit.
		std::string FirstBlobOfA() const
		{
			const Core::Result<Core::Repository> repository = Core::Repository::Open(Fixture.GetPath());
			REQUIRE(repository.HasValue());
			const Core::Result<Core::Diff> diff =
				repository.GetValue().QueryDiff(Core::DiffRequest{.Target = Core::DiffTarget::CommitToParent, .From = *Core::Oid::FromHex(Second)});
			REQUIRE(diff.HasValue());
			REQUIRE(diff.GetValue().Files.size() == 1);
			return diff.GetValue().Files[0].OldId.ToHex();
		}

		GitFixture Fixture;
		std::string First;
		std::string Second;
		std::string OnFeature;
	};
}

TEST_CASE("Blame at HEAD sends the file's bytes and names the commit of each line")
{
	ReviewHistory history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.blameAtHead", At(history.Fixture.GetPath(), {{"path", Linux(history.Fixture.GetPath() / "a.txt")}}));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue().Result.at("trackable") == true);
	const std::size_t headSize = answer.GetValue().Result.at("headSize");
	CHECK(answer.GetValue().Payload.substr(0, headSize) == "one\ntwo\n");
	const Core::Result<Core::Blame> blame = Core::BlameFromJson(json::parse(answer.GetValue().Payload.substr(headSize)));
	REQUIRE(blame.HasValue());
	CHECK(blame.GetValue().LineCount == 2);
	CHECK(blame.GetValue().Commits.size() == 2);
}

TEST_CASE("Blame at HEAD of an ignored file or one HEAD lacks is not trackable")
{
	ReviewHistory history;
	history.Fixture.Write("run.log", "x\n");
	history.Fixture.Write("new.txt", "x\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	for (const char* name : {"run.log", "new.txt"})
	{
		const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.blameAtHead", At(history.Fixture.GetPath(), {{"path", Linux(history.Fixture.GetPath() / name)}}));
		REQUIRE(answer.HasValue());
		CHECK(answer.GetValue().Result.at("trackable") == false);
	}
}

TEST_CASE("Blame at HEAD of an empty file asks libgit2 for no blame")
{
	ReviewHistory history;
	history.Fixture.Write("empty.txt", "");
	history.Fixture.Commit("empty");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.blameAtHead", At(history.Fixture.GetPath(), {{"path", Linux(history.Fixture.GetPath() / "empty.txt")}}));

	REQUIRE(answer.HasValue());
	CHECK(answer.GetValue().Result.at("headSize") == 0);
	const Core::Result<Core::Blame> blame = Core::BlameFromJson(json::parse(answer.GetValue().Payload));
	REQUIRE(blame.HasValue());
	CHECK(blame.GetValue().LineCount == 0);
	CHECK(blame.GetValue().Hunks.empty());
}

TEST_CASE("A line's history comes from the agent")
{
	ReviewHistory history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const Core::LineHistoryRequest request{.From = *Core::Oid::FromHex(history.Second), .File = "a.txt", .Line = 2};

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.lineHistory", At(history.Fixture.GetPath(), {{"request", Core::ToJson(request)}}));

	REQUIRE(answer.HasValue());
	const Core::Result<Core::LineHistory> lineHistory = Core::LineHistoryFromJson(json::parse(answer.GetValue().Payload));
	REQUIRE(lineHistory.HasValue());
	REQUIRE_FALSE(lineHistory.GetValue().Changes.empty());
	CHECK(lineHistory.GetValue().Changes[0].Commit.Id.ToHex() == history.Second);
}

TEST_CASE("A review range of a branch off master lists its own commit")
{
	ReviewHistory history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer =
		Call(handlers, "git.resolveRange", At(history.Fixture.GetPath(), {{"branch", "refs/heads/feature"}, {"base", "refs/heads/master"}}));

	REQUIRE(answer.HasValue());
	const json& result = answer.GetValue().Result;
	CHECK(result.at("problem") == "none");
	CHECK(result.at("mergeBase") == history.Second);
	CHECK(result.at("tip") == history.OnFeature);
	CHECK(result.at("complete") == true);
	const json commits = json::parse(answer.GetValue().Payload);
	REQUIRE(commits.size() == 1);
	CHECK(commits[0].at("id") == history.OnFeature);
}

TEST_CASE("A review range names a missing branch, a missing base and a history in common with nothing")
{
	ReviewHistory history;
	history.Fixture.StartOrphan("alone");
	history.Fixture.Write("c.txt", "x\n");
	history.Fixture.Commit("alone");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const auto problemOf = [&](const std::string& branch, const std::string& base) {
		const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.resolveRange", At(history.Fixture.GetPath(), {{"branch", branch}, {"base", base}}));
		REQUIRE(answer.HasValue());
		return answer.GetValue().Result.at("problem").get<std::string>();
	};

	CHECK(problemOf("refs/heads/gone", "refs/heads/master") == "branchMissing");
	CHECK(problemOf("refs/heads/feature", "refs/heads/gone") == "baseMissing");
	CHECK(problemOf("refs/heads/alone", "refs/heads/master") == "noCommonAncestor");
}

TEST_CASE("Blobs are read at the revisions asked, an absent one marked absent")
{
	ReviewHistory history;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const json reads = json::array({{{"revision", history.First}, {"path", "a.txt"}}, {{"revision", history.First}, {"path", "b.txt"}}, {{"revision", history.Second}, {"path", ""}}});

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.readBlobs", At(history.Fixture.GetPath(), {{"reads", reads}}));

	REQUIRE(answer.HasValue());
	const json& sides = answer.GetValue().Result.at("sides");
	REQUIRE(sides.size() == 3);
	CHECK(sides[0].at("present") == true);
	CHECK(sides[1].at("present") == false);
	CHECK(sides[2].at("present") == false);
	CHECK(answer.GetValue().Payload == "one\n");
}

TEST_CASE("The worktrees, a worktree's activity and its uncommitted count come from the agent")
{
	ReviewHistory history;
	history.Fixture.Write("a.txt", "changed\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> listed = Call(handlers, "git.worktrees", At(history.Fixture.GetPath()));
	REQUIRE(listed.HasValue());
	const Core::Result<std::vector<Core::WorktreeEntry>> worktrees = Core::WorktreesFromJson(json::parse(listed.GetValue().Payload));
	REQUIRE(worktrees.HasValue());
	REQUIRE(worktrees.GetValue().size() == 1);
	CHECK(worktrees.GetValue()[0].IsMain);

	const Core::Result<Agent::GitAnswer> activity = Call(handlers, "git.activity", At(history.Fixture.GetPath(), json::object(), "exactly"));
	REQUIRE(activity.HasValue());
	CHECK(activity.GetValue().Result.contains("headCommit"));
	CHECK(activity.GetValue().Result.contains("index"));

	const Core::Result<Agent::GitAnswer> count = Call(handlers, "git.countUncommitted", At(history.Fixture.GetPath(), json::object(), "exactly"));
	REQUIRE(count.HasValue());
	CHECK(count.GetValue().Result.at("changed") == 1);
	CHECK(count.GetValue().Result.contains("newest"));
}

TEST_CASE("Since viewed reads the viewed blob and the current version from a tip or a worktree")
{
	ReviewHistory history;
	history.Fixture.Write("a.txt", "on disk\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const std::string viewed = history.FirstBlobOfA();

	const Core::Result<Agent::GitAnswer> fromTip =
		Call(handlers, "git.sinceViewed", At(history.Fixture.GetPath(), {{"viewed", viewed}, {"tip", history.Second}, {"path", "a.txt"}}));
	const Core::Result<Agent::GitAnswer> fromWorktree =
		Call(handlers, "git.sinceViewed", At(history.Fixture.GetPath(), {{"viewed", viewed}, {"worktree", Linux(history.Fixture.GetPath())}, {"path", "a.txt"}}));

	REQUIRE(fromTip.HasValue());
	CHECK(fromTip.GetValue().Payload == "one\none\ntwo\n");
	REQUIRE(fromWorktree.HasValue());
	CHECK(fromWorktree.GetValue().Payload == "one\non disk\n");
}

TEST_CASE("Since viewed of a file the worktree no longer has is a failed read, not an absence")
{
	ReviewHistory history;
	std::filesystem::remove(history.Fixture.GetPath() / "a.txt");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.sinceViewed",
		At(history.Fixture.GetPath(), {{"viewed", history.FirstBlobOfA()}, {"worktree", Linux(history.Fixture.GetPath())}, {"path", "a.txt"}}));

	REQUIRE_FALSE(answer.HasValue());
	CHECK(answer.GetError().Code == Core::ErrorCode::IoFailure);
}

TEST_CASE("Blame at HEAD of a file too large or too binary to show sends its bytes and blames nothing")
{
	ReviewHistory history;
	history.Fixture.Write("big.txt", std::string(Core::MaximumDiffTextSize + 1, 'x'));
	history.Fixture.Write("image.bin", std::string{"PNG\0\0\x01", 6});
	history.Fixture.Commit("large and binary");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	for (const char* name : {"big.txt", "image.bin"})
	{
		const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.blameAtHead", At(history.Fixture.GetPath(), {{"path", Linux(history.Fixture.GetPath() / name)}}));
		REQUIRE(answer.HasValue());
		CHECK(answer.GetValue().Result.at("trackable") == true);
		const std::size_t headSize = answer.GetValue().Result.at("headSize");
		const Core::Result<Core::Blame> blame = Core::BlameFromJson(json::parse(answer.GetValue().Payload.substr(headSize)));
		REQUIRE(blame.HasValue());
		CHECK(blame.GetValue().Hunks.empty());
	}
}
