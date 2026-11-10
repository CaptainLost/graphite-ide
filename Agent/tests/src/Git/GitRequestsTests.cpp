#include <doctest/doctest.h>

#include "Git/GitRequests.h"

#include "Core/Remote/GitCodec.h"

#include "GitFixture.h"

#include <filesystem>
#include <string>

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

	Core::WorkingTreeStatus StatusOf(const Agent::GitHandlers& handlers, const GitFixture& fixture)
	{
		const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.status", At(fixture));
		REQUIRE(answer.HasValue());
		const Core::Result<Core::WorkingTreeStatus> status = Core::WorkingTreeStatusFromJson(json::parse(answer.GetValue().Payload));
		REQUIRE(status.HasValue());
		return status.GetValue();
	}

	const Core::FileStatus* Find(const Core::WorkingTreeStatus& status, const std::string& path)
	{
		for (const Core::FileStatus& entry : status.Entries)
		{
			if (entry.Path.generic_string() == path) // not a path conversion
			{
				return &entry;
			}
		}

		return nullptr;
	}
}

TEST_CASE("git.status lists a modified and an untracked file, marking an untracked folder that holds a checkout")
{
	GitFixture fixture;
	fixture.Write("tracked.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("tracked.txt", "two\n");
	fixture.Write("new file.txt", "x\n");
	// A linked worktree's shape: a .git file beside its files.
	fixture.Write("nested/.git", "gitdir: /nowhere\n");
	fixture.Write("nested/inside.txt", "x\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::WorkingTreeStatus status = StatusOf(handlers, fixture);

	REQUIRE(Find(status, "tracked.txt") != nullptr);
	CHECK(Find(status, "tracked.txt")->WorkTree == Core::StatusFlags::Modified);
	REQUIRE(Find(status, "new file.txt") != nullptr);
	CHECK(Find(status, "new file.txt")->IsUntracked);
	const Core::FileStatus* nested = Find(status, "nested");
	REQUIRE(nested != nullptr);
	CHECK(nested->HoldsCheckout);
}

TEST_CASE("git.stage then git.commit records the change, and git.head names the new commit")
{
	GitFixture fixture;
	fixture.SetIdentity("Tester", "tester@example.com");
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	REQUIRE(Call(handlers, "git.stage", At(fixture, {{"paths", json::array({"a.txt"})}})).HasValue());
	CHECK(Find(StatusOf(handlers, fixture), "a.txt")->Index == Core::StatusFlags::Modified);

	const Core::Result<Agent::GitAnswer> committed =
		Call(handlers, "git.commit", At(fixture, {{"request", Core::ToJson(Core::CommitRequest{.Message = "second"})}}));
	REQUIRE(committed.HasValue());
	const std::string oid = committed.GetValue().Result.at("oid");

	const Core::Result<Agent::GitAnswer> head = Call(handlers, "git.head", At(fixture));
	REQUIRE(head.HasValue());
	const Core::Result<Core::HeadState> state = Core::HeadStateFromJson(json::parse(head.GetValue().Payload));
	REQUIRE(state.HasValue());
	REQUIRE(state.GetValue().Commit.has_value());
	CHECK(state.GetValue().Commit->ToHex() == oid);
	CHECK(Find(StatusOf(handlers, fixture), "a.txt") == nullptr);
}

TEST_CASE("git.unstage and git.discard take a change back out of the index and the working tree")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	REQUIRE(Call(handlers, "git.stage", At(fixture, {{"paths", json::array({"a.txt"})}})).HasValue());
	REQUIRE(Call(handlers, "git.unstage", At(fixture, {{"paths", json::array({"a.txt"})}})).HasValue());
	CHECK(Find(StatusOf(handlers, fixture), "a.txt")->Index == Core::StatusFlags::None);

	REQUIRE(Call(handlers, "git.discard", At(fixture, {{"paths", json::array({"a.txt"})}})).HasValue());
	CHECK(Find(StatusOf(handlers, fixture), "a.txt") == nullptr);
}

TEST_CASE("git.diff of the working tree shows the changed line")
{
	GitFixture fixture;
	fixture.Write("a.txt", "one\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "two\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer =
		Call(handlers, "git.diff", At(fixture, {{"request", Core::ToJson(Core::DiffRequest{.Target = Core::DiffTarget::WorkTreeToHead})}}));

	REQUIRE(answer.HasValue());
	const Core::Result<Core::Diff> diff = Core::DiffFromJson(json::parse(answer.GetValue().Payload));
	REQUIRE(diff.HasValue());
	REQUIRE(diff.GetValue().Files.size() == 1);
	REQUIRE_FALSE(diff.GetValue().Files[0].Hunks.empty());
}

TEST_CASE("git.readSides returns the sides asked for, an absent one marked absent")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Write("a.txt", "on disk\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.readSides",
													   At(fixture, {{"reads", json::array({{{"side", "head"}, {"path", "a.txt"}},
																						   {{"side", "workTree"}, {"path", "a.txt"}},
																						   {{"side", "head"}, {"path", "missing.txt"}}})}}));

	REQUIRE(answer.HasValue());
	const json& sides = answer.GetValue().Result.at("sides");
	REQUIRE(sides.size() == 3);
	CHECK(sides[0].at("present") == true);
	CHECK(sides[1].at("present") == true);
	CHECK(sides[2].at("present") == false);
	CHECK(answer.GetValue().Payload == "committed\non disk\n");
	CHECK(sides[0].at("size") == 10);
}

TEST_CASE("git.bases of an ignored file is not trackable, and of a tracked one has HEAD and the index")
{
	GitFixture fixture;
	fixture.Write(".gitignore", "*.log\n");
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	fixture.Write("debug.log", "x\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	const Core::Result<Agent::GitAnswer> ignored =
		Call(handlers, "git.bases", At(fixture, {{"path", (fixture.GetPath() / "debug.log").string()}})); // not a path conversion
	REQUIRE(ignored.HasValue());
	CHECK(ignored.GetValue().Result.at("trackable") == false);

	const Core::Result<Agent::GitAnswer> tracked =
		Call(handlers, "git.bases", At(fixture, {{"path", (fixture.GetPath() / "a.txt").string()}})); // not a path conversion
	REQUIRE(tracked.HasValue());
	CHECK(tracked.GetValue().Result.at("trackable") == true);
	CHECK(tracked.GetValue().Payload == "committed\ncommitted\n");
}

TEST_CASE("git.workingDirectory answers the repository's folder, and a folder outside any repository is not found")
{
	GitFixture fixture;
	fixture.Write("sub/a.txt", "x\n");
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();

	json fromBelow = At(fixture);
	fromBelow["root"] = (fixture.GetPath() / "sub").string(); // not a path conversion
	const Core::Result<Agent::GitAnswer> answer = Call(handlers, "git.workingDirectory", fromBelow);
	REQUIRE(answer.HasValue());
	CHECK(std::filesystem::path{answer.GetValue().Result.at("path").get<std::string>()} == std::filesystem::weakly_canonical(fixture.GetPath()));

	json nowhere = At(fixture);
	nowhere["root"] = "/";
	const Core::Result<Agent::GitAnswer> outside = Call(handlers, "git.workingDirectory", nowhere);
	REQUIRE_FALSE(outside.HasValue());
	CHECK(outside.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("A root reached through a symbolic link keeps its spelling, and its files stay trackable")
{
	GitFixture fixture;
	fixture.Write("a.txt", "committed\n");
	fixture.Commit("first");
	TempDirectory links;
	const std::filesystem::path linked = links.GetPath() / "linked";
	std::filesystem::create_directory_symlink(fixture.GetPath(), linked);
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const json at{{"root", linked.string()}, {"reach", "up"}}; // not a path conversion

	const Core::Result<Agent::GitAnswer> directory = Call(handlers, "git.workingDirectory", at);
	REQUIRE(directory.HasValue());
	CHECK(directory.GetValue().Result.at("path") == linked.string()); // not a path conversion

	json bases = at;
	bases["path"] = (linked / "a.txt").string(); // not a path conversion
	const Core::Result<Agent::GitAnswer> tracked = Call(handlers, "git.bases", bases);
	REQUIRE(tracked.HasValue());
	CHECK(tracked.GetValue().Result.at("trackable") == true);
}

TEST_CASE("git.bases in a folder with no repository is not trackable, rather than an error")
{
	TempDirectory directory;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const json at{{"root", directory.GetPath().string()}, {"reach", "exactly"}, {"path", (directory.GetPath() / "a.txt").string()}}; // not a path conversion

	const Core::Result<Agent::GitAnswer> bases = Call(handlers, "git.bases", at);

	REQUIRE(bases.HasValue());
	CHECK(bases.GetValue().Result.at("trackable") == false);
}

TEST_CASE("git.init makes a repository in an empty folder and refuses inside one")
{
	TempDirectory directory;
	const Agent::GitHandlers handlers = Agent::DefaultGitHandlers();
	const json at{{"root", directory.GetPath().string()}, {"reach", "exactly"}}; // not a path conversion

	REQUIRE(Call(handlers, "git.init", at).HasValue());
	CHECK(std::filesystem::exists(directory.GetPath() / ".git"));
	CHECK_FALSE(Call(handlers, "git.init", at).HasValue());
}
