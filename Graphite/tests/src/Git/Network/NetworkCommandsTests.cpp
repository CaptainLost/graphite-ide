#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/Network/NetworkCommands.h"

#include "Core/Command/CommandRegistry.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using Graphite::NetworkButtons;
using Graphite::NetworkOperation;
using Graphite::NetworkState;

namespace {

	const std::filesystem::path kFolder = "C:/work/graphite/";
	const std::chrono::sys_seconds kNow{std::chrono::seconds{1'800'000'000}};

	Core::Branch HeadBranch(const std::string& name, std::optional<std::string> upstream = std::nullopt, std::size_t ahead = 0,
							std::size_t behind = 0)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = "refs/heads/" + name;
		branch.IsHead = true;
		branch.Upstream = std::move(upstream);
		branch.Ahead = ahead;
		branch.Behind = behind;
		branch.DistanceKnown = branch.Upstream.has_value();
		return branch;
	}

	Core::HeadState OnBranch(const std::string& name)
	{
		Core::HeadState head;
		head.Kind = Core::HeadKind::Branch;
		head.BranchName = name;
		head.Commit = OidWithFirstByte(1);
		return head;
	}

	NetworkButtons Describe(const Core::HeadState& head, const std::vector<Core::Branch>& branches,
							const std::vector<Core::Remote>& remotes, const NetworkState& state = {}, bool gitFound = true,
							bool folderTrusted = true)
	{
		return Graphite::DescribeNetworkButtons(Graphite::WindowBarInput{kFolder, &head, &branches, &remotes, false}, state,
												std::nullopt, kNow,
												Graphite::NetworkAccess{.GitFound = gitFound, .FolderTrusted = folderTrusted});
	}

	constexpr const char* kFolderNotTrustedSentence =
		"Fetch, Pull and Push run git, which runs this folder's hooks and the commands its git config names. Trust the folder "
		"to use them.";

	struct Registered
	{
		explicit Registered(NetworkButtons shown) : Buttons(std::move(shown))
		{
			Graphite::RegisterNetworkCommands(Commands, Graphite::NetworkCommandServices{.Buttons = [this] { return Buttons; },
																						  .Fetch = [this] { ++Fetches; },
																						  .Pull = [this] { ++Pulls; },
																						  .Push = [this] { ++Pushes; }});
		}

		bool EnabledNow(const char* id) const { return Core::IsCommandEnabled(*Commands.Find(id)); }

		Core::CommandRegistry Commands;
		NetworkButtons Buttons;
		int Fetches = 0;
		int Pulls = 0;
		int Pushes = 0;
	};
}

TEST_CASE("Commands: fetch, pull and push are registered and enabled as the buttons are")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const std::vector<Core::Branch> tracking = {HeadBranch("main", "origin/main", 1, 2)};
	const std::vector<Core::Branch> untracked = {HeadBranch("main")};
	const NetworkState idle;
	const NetworkState fetching{.Running = NetworkOperation::Fetch, .Serial = 1, .Cancellable = true};

	for (const std::vector<Core::Branch>* branches : {&tracking, &untracked})
	{
		for (const NetworkState* state : {&idle, &fetching})
		{
			const NetworkButtons buttons = Describe(head, *branches, remotes, *state);
			Registered registered(buttons);

			REQUIRE(registered.Commands.Find("git.fetch") != nullptr);
			REQUIRE(registered.Commands.Find("git.pull") != nullptr);
			REQUIRE(registered.Commands.Find("git.push") != nullptr);

			// A button that runs is enabled to cancel; a command starts an operation, so it is not.
			CHECK(registered.EnabledNow("git.fetch") == (buttons.Fetch.Enabled && !buttons.Fetch.Running));
			CHECK(registered.EnabledNow("git.pull") == (buttons.Pull.Enabled && !buttons.Pull.Running));
			CHECK(registered.EnabledNow("git.push") == (buttons.Push.Enabled && !buttons.Push.Running));
		}
	}
}

TEST_CASE("Commands: they are named Git: Fetch, Git: Pull and Git: Push")
{
	Registered registered(NetworkButtons{});

	CHECK(registered.Commands.Find("git.fetch")->Label == "Git: Fetch");
	CHECK(registered.Commands.Find("git.pull")->Label == "Git: Pull");
	CHECK(registered.Commands.Find("git.push")->Label == "Git: Push");
}

TEST_CASE("Commands: with nothing listed yet all three are disabled, and with a tracking branch all three are enabled")
{
	Registered waiting(NetworkButtons{});

	CHECK_FALSE(waiting.EnabledNow("git.fetch"));
	CHECK_FALSE(waiting.EnabledNow("git.pull"));
	CHECK_FALSE(waiting.EnabledNow("git.push"));

	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main", "origin/main", 1, 2)};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	Registered ready(Describe(head, branches, remotes));

	CHECK(ready.EnabledNow("git.fetch"));
	CHECK(ready.EnabledNow("git.pull"));
	CHECK(ready.EnabledNow("git.push"));
}

TEST_CASE("Commands: while any operation runs none can start another")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main", "origin/main", 1, 2)};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};

	for (const NetworkOperation operation :
		 {NetworkOperation::Fetch, NetworkOperation::Pull, NetworkOperation::Push, NetworkOperation::FullHistory})
	{
		Registered registered(Describe(head, branches, remotes, NetworkState{.Running = operation, .Serial = 1, .Cancellable = true}));

		CHECK_FALSE(registered.EnabledNow("git.fetch"));
		CHECK_FALSE(registered.EnabledNow("git.pull"));
		CHECK_FALSE(registered.EnabledNow("git.push"));
	}
}

TEST_CASE("Commands: push with several remotes and no upstream on a remote is disabled and says where to choose")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}, {"backup", "ssh://host/a.git"}};
	const NetworkButtons buttons = Describe(head, branches, remotes);

	REQUIRE(buttons.Push.AsksWhichRemote);
	REQUIRE(buttons.Push.Enabled);

	Registered registered(buttons);

	CHECK_FALSE(registered.EnabledNow("git.push"));
	CHECK(registered.Commands.Find("git.push")->DisabledNote == "Choose the remote from Push in the Git window.");
	CHECK(Graphite::CommandStateOfPush(buttons).Note == "Choose the remote from Push in the Git window.");
	CHECK(registered.EnabledNow("git.fetch"));
}

TEST_CASE("Commands: a disabled command says what the bar's button says")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const NetworkButtons buttons = Describe(head, branches, remotes);

	REQUIRE_FALSE(buttons.Pull.Enabled);
	CHECK(Graphite::CommandStateOfPull(buttons).Note == buttons.Pull.Tooltip);
	CHECK_FALSE(Graphite::CommandStateOfPull(buttons).Note.empty());
	CHECK(Graphite::CommandStateOfFetch(buttons).Note.empty());
}

TEST_CASE("Commands: Fetch full history is disabled for Fetch's own reasons in Fetch's words, and while any operation runs")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main", "origin/main", 1, 2)};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	const std::vector<Core::Remote> none;
	const NetworkState idle;

	const Graphite::NetworkCommandState ready = Graphite::CommandStateOfFullHistory(Describe(head, branches, remotes), idle);
	CHECK(ready.Enabled);
	CHECK(ready.Note.empty());

	const Graphite::NetworkCommandState noRemote = Graphite::CommandStateOfFullHistory(Describe(head, branches, none), idle);
	CHECK_FALSE(noRemote.Enabled);
	CHECK(noRemote.Note == "This repository has no remote.");

	constexpr bool kGitMissing = false;
	const Graphite::NetworkCommandState noGit =
		Graphite::CommandStateOfFullHistory(Describe(head, branches, remotes, idle, kGitMissing), idle);
	CHECK_FALSE(noGit.Enabled);
	CHECK(noGit.Note == "Fetch, Pull and Push need git, which was not found on PATH or where Git for Windows installs it.");

	// Any operation running holds it, the full history's own included, and says which runs.
	const NetworkState pulling{.Running = NetworkOperation::Pull, .Serial = 1, .Cancellable = true};
	const Graphite::NetworkCommandState whilePulling =
		Graphite::CommandStateOfFullHistory(Describe(head, branches, remotes, pulling), pulling);
	CHECK_FALSE(whilePulling.Enabled);
	CHECK(whilePulling.Note == "Pulling\xE2\x80\xA6");

	const NetworkState deepening{.Running = NetworkOperation::FullHistory, .Serial = 2, .Percent = 45, .Cancellable = true};
	const Graphite::NetworkCommandState whileDeepening =
		Graphite::CommandStateOfFullHistory(Describe(head, branches, remotes, deepening), deepening);
	CHECK_FALSE(whileDeepening.Enabled);
	CHECK(whileDeepening.Note == "Fetching full history\xE2\x80\xA6 45%");

	for (const NetworkOperation operation : {NetworkOperation::Fetch, NetworkOperation::Push})
	{
		const NetworkState running{.Running = operation, .Serial = 3, .Cancellable = true};
		CHECK_FALSE(Graphite::CommandStateOfFullHistory(Describe(head, branches, remotes, running), running).Enabled);
	}
}

TEST_CASE("Commands: in a folder not trusted the three commands and Fetch full history are disabled, saying why")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main", "origin/main", 1, 2)};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	constexpr bool kGitFound = true;
	constexpr bool kNotTrusted = false;
	const NetworkButtons untrusted = Describe(head, branches, remotes, {}, kGitFound, kNotTrusted);

	Registered registered(untrusted);
	CHECK_FALSE(registered.EnabledNow("git.fetch"));
	CHECK_FALSE(registered.EnabledNow("git.pull"));
	CHECK_FALSE(registered.EnabledNow("git.push"));
	CHECK(Graphite::CommandStateOfFetch(untrusted).Note == kFolderNotTrustedSentence);
	CHECK(Graphite::CommandStateOfPull(untrusted).Note == kFolderNotTrustedSentence);
	CHECK(Graphite::CommandStateOfPush(untrusted).Note == kFolderNotTrustedSentence);

	const Graphite::NetworkCommandState fullHistory = Graphite::CommandStateOfFullHistory(untrusted, NetworkState{});
	CHECK_FALSE(fullHistory.Enabled);
	CHECK(fullHistory.Note == kFolderNotTrustedSentence);

	// Trusted, the same repository runs all four.
	const NetworkButtons trusted = Describe(head, branches, remotes);
	Registered trustedCommands(trusted);
	CHECK(trustedCommands.EnabledNow("git.fetch"));
	CHECK(trustedCommands.EnabledNow("git.pull"));
	CHECK(trustedCommands.EnabledNow("git.push"));
	CHECK(Graphite::CommandStateOfFullHistory(trusted, NetworkState{}).Enabled);
}

TEST_CASE("Commands: running one starts its operation")
{
	const Core::HeadState head = OnBranch("main");
	const std::vector<Core::Branch> branches = {HeadBranch("main", "origin/main")};
	const std::vector<Core::Remote> remotes = {{"origin", "https://example.com/a.git"}};
	Registered registered(Describe(head, branches, remotes));

	CHECK(registered.Commands.Run("git.fetch"));
	CHECK(registered.Commands.Run("git.pull"));
	CHECK(registered.Commands.Run("git.push"));
	CHECK(registered.Fetches == 1);
	CHECK(registered.Pulls == 1);
	CHECK(registered.Pushes == 1);

	registered.Buttons = NetworkButtons{};

	CHECK_FALSE(registered.Commands.Run("git.fetch"));
	CHECK(registered.Fetches == 1);
}
