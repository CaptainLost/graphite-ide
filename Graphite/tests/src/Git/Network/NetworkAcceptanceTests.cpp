#include <doctest/doctest.h>

#include "Git/GitModel.h"
#include "Git/Network/NetworkCommands.h"
#include "Git/Network/NetworkModel.h"
#include "Git/Network/NetworkText.h"
#include "Git/RepositoryGitPort.h"

#include "TextFixtures.h"

#include "Core/Base/Timestep.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Command/GitExecutable.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Command/GitRunner.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"
#include "Core/Jobs/JobQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// The acceptance of fetch, pull and push: each case runs through the app's own port and models, as the window's buttons
// run them, against local repositories only. A "server" is a bare repository named by its path, a peer clone moves it on,
// and the app works in a clone of it. Nothing here contacts a network.
//
// Skipped by default. Run with
//   GRAPHITE_NETWORK=1 GRAPHITE_NETWORK_ROOT=<folder> GraphiteTests -tc="Network acceptance:*" --no-skip
// Each case makes its repositories afresh in a folder of its own under the root and leaves them there to be looked at.
// The slow fetch's server, <root>\large.git, is made once, of GRAPHITE_NETWORK_LARGE_MIB files of one MiB of random bytes
// (512 by default), and kept.

namespace {

	constexpr char kBranch[] = "main";
	constexpr char kRemote[] = "origin";

	// The app's frame, and the waits of a case that only needs its operation to land.
	constexpr float kFrameSeconds = 1.0f / 60.0f;
	constexpr std::chrono::milliseconds kFramePeriod{16};
	constexpr std::chrono::milliseconds kQuickFramePeriod{2};
	constexpr std::chrono::milliseconds kCancelPollPeriod{1};
	constexpr std::chrono::seconds kPatience{300};

	constexpr int kDefaultLargeMebibytes = 512;
	constexpr std::size_t kMebibyte = 1024 * 1024;
	constexpr std::uint64_t kLargeSeed = 20261108;

	// How long a fetch has run, counting, before the cancel cases cancel it.
	constexpr std::chrono::milliseconds kRunBeforeCancel{1500};
	constexpr double kMostCancelMilliseconds = 500.0;
	constexpr double kMostFrameCostBeyondIdleMilliseconds = 1.0;
	constexpr int kIdleFrames = 120;

	double Milliseconds(std::chrono::steady_clock::duration duration)
	{
		return std::chrono::duration<double, std::milli>(duration).count();
	}

	std::string Trimmed(std::string text)
	{
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		{
			text.pop_back();
		}

		return text;
	}

	void IgnoreLine([[maybe_unused]] std::string_view line)
	{
	}

	// The folder the acceptance works in. Every case fails at once, saying why, unless GRAPHITE_NETWORK=1 asks for it.
	std::filesystem::path AcceptanceRoot()
	{
		REQUIRE_MESSAGE(EnvironmentText("GRAPHITE_NETWORK") == "1", "GRAPHITE_NETWORK=1 is not set");
		const std::string root = EnvironmentText("GRAPHITE_NETWORK_ROOT");
		REQUIRE_MESSAGE(!root.empty(), "GRAPHITE_NETWORK_ROOT is not set");
		return std::filesystem::path{root};
	}

	std::filesystem::path FoundGit()
	{
		const std::optional<std::filesystem::path> git = Core::FindGit();
		REQUIRE_MESSAGE(git.has_value(), "git is neither on PATH nor where Git for Windows installs it");
		return *git;
	}

	// An empty file under the root, the setup's git's global config.
	std::filesystem::path EmptyGlobalConfig()
	{
		const std::filesystem::path config = AcceptanceRoot() / "empty-global-gitconfig";
		const std::ofstream emptied{config, std::ios::trunc};
		REQUIRE(emptied.is_open());
		return config;
	}

	// The git the acceptance makes and moves its repositories with, apart from the app's. It reads no system config and an
	// empty global one, so nothing of the user's (commit.gpgsign, core.hooksPath, init.defaultBranch) can stop or change
	// it, and its author is given on the command line. The app's port runs git with the user's own config, as in the app.
	class SetupGit
	{
	public:
		SetupGit(std::filesystem::path git, const std::filesystem::path& emptyGlobalConfig)
			: m_Git(std::move(git)), m_Environment(Core::GitEnvironment(false))
		{
			m_Environment.push_back(Core::EnvironmentVariable{.Name = "GIT_CONFIG_NOSYSTEM", .Value = "1"});
			m_Environment.push_back(Core::EnvironmentVariable{.Name = "GIT_CONFIG_GLOBAL", .Value = Core::ToUtf8(emptyGlobalConfig)});
		}

		std::string Run(const std::filesystem::path& directory, std::vector<std::string> arguments) const
		{
			const Core::GitOutput output = RunAnyway(directory, std::move(arguments));
			INFO(output.Errors);
			REQUIRE(output.ExitCode == 0);
			return Trimmed(output.Out);
		}

		Core::GitOutput RunAnyway(const std::filesystem::path& directory, std::vector<std::string> arguments) const
		{
			const Core::GitRun run{.Arguments = std::move(arguments), .WorkingDirectory = directory, .Environment = m_Environment};
			const std::atomic<bool> neverCancelled{false};
			Core::Result<Core::GitOutput> finished = Core::RunGit(m_Git, run, neverCancelled, IgnoreLine);
			REQUIRE(finished.HasValue());
			return std::move(finished).GetValue();
		}

		bool HasRef(const std::filesystem::path& repository, const std::string& ref) const
		{
			return RunAnyway(repository, {"rev-parse", "--verify", "--quiet", ref}).ExitCode == 0;
		}

		std::string Head(const std::filesystem::path& repository) const { return Run(repository, {"rev-parse", "HEAD"}); }

		void CommitFile(const std::filesystem::path& repository, const std::string& file, const std::string& contents) const
		{
			WriteFile(repository / Core::FromUtf8(file), contents);
			Run(repository, {"add", "--", file});
			Run(repository, {"-c", "user.name=Acceptance", "-c", "user.email=acceptance@example.com", "commit", "-q", "-m",
							 "change " + file});
		}

		static void WriteFile(const std::filesystem::path& path, const std::string& contents)
		{
			std::ofstream out{path, std::ios::binary | std::ios::trunc};
			REQUIRE(out.is_open());
			out << contents;
		}

		static std::string ReadFile(const std::filesystem::path& path)
		{
			std::ifstream in{path, std::ios::binary};
			return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
		}

		const std::filesystem::path& Executable() const { return m_Git; }

	private:
		std::filesystem::path m_Git;
		std::vector<Core::EnvironmentVariable> m_Environment;
	};

	// One case's folder under the root, emptied of what an earlier run left, and the repositories made in it.
	class Scene
	{
	public:
		explicit Scene(const std::string& name) : Git(FoundGit(), EmptyGlobalConfig()), m_Folder(AcceptanceRoot() / name)
		{
			std::error_code removeError;
			std::filesystem::remove_all(m_Folder, removeError);
			REQUIRE_MESSAGE(!removeError, removeError.message());
			std::filesystem::create_directories(m_Folder);
			MESSAGE("scene ", Core::ToUtf8(m_Folder), ", git ", Core::ToUtf8(Git.Executable()));

			// The setup's git sees none of the machine's or the user's config: outside a repository it has none at all.
			REQUIRE(Git.RunAnyway(m_Folder, {"config", "list", "--show-scope"}).Out.empty());
		}

		const std::filesystem::path& Folder() const { return m_Folder; }

		// Where the app keeps what it stores of a repository, apart from the user's own.
		std::filesystem::path Store() const { return m_Folder / "store"; }

		// A bare repository whose HEAD names main, as a server's does.
		std::filesystem::path Server(const std::string& name) const
		{
			const std::filesystem::path server = m_Folder / (name + ".git");
			Git.Run(m_Folder, {"init", "-q", "--bare", "--initial-branch=main", Core::ToUtf8(server)});
			return server;
		}

		// A repository with the server as origin and main pushed with one commit: what moves the server on.
		std::filesystem::path Peer(const std::filesystem::path& server, const std::string& name) const
		{
			const std::filesystem::path peer = EmptyClone(server, name);
			Git.CommitFile(peer, "a.txt", "one\n");
			Git.Run(peer, {"push", "-q", "-u", kRemote, kBranch});
			return peer;
		}

		// git init and the server added as origin, nothing fetched yet.
		std::filesystem::path EmptyClone(const std::filesystem::path& server, const std::string& name) const
		{
			const std::filesystem::path clone = m_Folder / Core::FromUtf8(name);
			Git.Run(m_Folder, {"init", "-q", "--initial-branch=main", Core::ToUtf8(clone)});
			Git.Run(clone, {"remote", "add", kRemote, Core::ToUtf8(server)});
			return clone;
		}

		// A clone as the user makes one: main checked out, following origin/main.
		std::filesystem::path Clone(const std::string& from, const std::string& name, std::vector<std::string> options = {}) const
		{
			const std::filesystem::path clone = m_Folder / Core::FromUtf8(name);
			std::vector<std::string> arguments{"clone", "-q"};
			arguments.insert(arguments.end(), options.begin(), options.end());
			arguments.push_back(from);
			arguments.push_back(Core::ToUtf8(clone));
			Git.Run(m_Folder, std::move(arguments));
			return clone;
		}

		SetupGit Git;

	private:
		std::filesystem::path m_Folder;
	};

	// What an operation left once it landed: done or cancelled, or failed.
	struct Landing
	{
		std::optional<Graphite::NetworkDone> Done;
		std::optional<Graphite::NetworkFailureReport> Failure;
		double Milliseconds = 0.0;
	};

	std::string Describe(const Landing& landing)
	{
		if (landing.Done.has_value())
		{
			return "done: " + Graphite::DoneText(*landing.Done);
		}

		if (landing.Failure.has_value())
		{
			return "failed: " + Graphite::FailureTitle(landing.Failure->Operation) + ": " + landing.Failure->Error.Message;
		}

		return "nothing landed";
	}

	// Each frame's cost, in milliseconds, and how many frames saw a new percent.
	struct FrameCosts
	{
		std::vector<double> Milliseconds;
		std::size_t PercentChanges = 0;
		double Seconds = 0.0;

		double Mean() const
		{
			if (Milliseconds.empty())
			{
				return 0.0;
			}

			double total = 0.0;

			for (const double cost : Milliseconds)
			{
				total += cost;
			}

			return total / static_cast<double>(Milliseconds.size());
		}

		// The cost `share` of the frames stay at or under; Quantile(1.0) is the most a frame cost.
		double Quantile(double share) const
		{
			if (Milliseconds.empty())
			{
				return 0.0;
			}

			std::vector<double> sorted = Milliseconds;
			std::ranges::sort(sorted);
			const std::size_t index = std::min(sorted.size() - 1, static_cast<std::size_t>(share * static_cast<double>(sorted.size())));
			return sorted[index];
		}
	};

	std::string Summary(const FrameCosts& costs)
	{
		return std::to_string(costs.Milliseconds.size()) + " frames, mean " + std::to_string(costs.Mean() * 1000.0) + " us, p50 " +
			   std::to_string(costs.Quantile(0.5) * 1000.0) + " us, p99 " + std::to_string(costs.Quantile(0.99) * 1000.0) +
			   " us, most " + std::to_string(costs.Quantile(1.0) * 1000.0) + " us";
	}

	// The app's git model and network model over the real port, on queues as the app gives them, driven frame by frame from
	// the test's thread as the app's UI thread drives them. A frame does the network's share of the UI thread's work: both
	// models' updates and the words of the window bar's buttons and of the status bar's activity.
	// WARNING: The queues are declared last, so they join their workers before the models and the port go.
	class AppNetwork
	{
	public:
		AppNetwork(const std::filesystem::path& work, const std::filesystem::path& store) : Port(work, store) { Git.SetRoot(work); }

		~AppNetwork() { Network.Detach(); }

		AppNetwork(const AppNetwork&) = delete;
		AppNetwork& operator=(const AppNetwork&) = delete;

		// The frame's cost, in milliseconds.
		double Frame()
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

			Git.OnUpdate(Core::Timestep{kFrameSeconds});
			Network.OnUpdate(Core::Timestep{kFrameSeconds});
			const Graphite::NetworkButtons buttons = Graphite::DescribeNetworkButtonsNow(Git, Network, false);
			const Graphite::NetworkActivity activity = Graphite::DescribeNetworkActivity(Network.GetState(), Network.GetShownDone());

			// Kept, so nothing of the words is left unmade.
			m_WordsMade += buttons.Fetch.Label.size() + buttons.Fetch.Tooltip.size() + buttons.Pull.Tooltip.size() +
						   buttons.Push.Tooltip.size() + activity.Running.value_or(std::string{}).size();

			return Milliseconds(std::chrono::steady_clock::now() - start);
		}

		// HEAD, the branches and the remotes read again, as after a change on disk, and landed: what the window bar's
		// buttons are described from.
		void List()
		{
			const Core::Ref<const Core::HeadState> headBefore = Git.GetHead();
			Git.RequestHead();
			Git.RequestBranches();
			const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kPatience;

			do
			{
				std::this_thread::sleep_for(kQuickFramePeriod);
				Frame();
			} while (!Listed(headBefore) && std::chrono::steady_clock::now() < deadline);

			REQUIRE(Git.GetHead() != nullptr);
			REQUIRE(Git.GetBranches() != nullptr);
			REQUIRE(Git.GetRemotes() != nullptr);
		}

		// Frames until the operation asked lands, done or failed.
		Landing RunToEnd(std::chrono::milliseconds framePeriod = kQuickFramePeriod, FrameCosts* costs = nullptr)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			const std::chrono::steady_clock::time_point deadline = start + kPatience;
			std::optional<int> lastPercent;
			Landing landing;

			while (std::chrono::steady_clock::now() < deadline)
			{
				const double cost = Frame();

				if (costs != nullptr)
				{
					costs->Milliseconds.push_back(cost);
					const std::optional<int> percent = Network.GetState().Percent;

					if (percent.has_value() && percent != lastPercent)
					{
						++costs->PercentChanges;
					}

					lastPercent = percent;
				}

				landing.Done = Network.TakeDone();
				landing.Failure = Network.TakeFailure();

				if (landing.Done.has_value() || landing.Failure.has_value())
				{
					break;
				}

				std::this_thread::sleep_for(framePeriod);
			}

			landing.Milliseconds = Milliseconds(std::chrono::steady_clock::now() - start);

			if (costs != nullptr)
			{
				costs->Seconds = landing.Milliseconds / 1000.0;
			}

			INFO(Describe(landing));
			REQUIRE((landing.Done.has_value() || landing.Failure.has_value()));
			return landing;
		}

		// Frames until the running operation has counted for `after`, git's percent shown; false when it never does.
		bool RunUntilCounting(std::chrono::milliseconds after)
		{
			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			const std::chrono::steady_clock::time_point deadline = start + kPatience;

			while (std::chrono::steady_clock::now() < deadline)
			{
				Frame();
				const Graphite::NetworkState state = Network.GetState();

				if (!state.Running.has_value())
				{
					return false;
				}

				if (state.Percent.has_value() && std::chrono::steady_clock::now() - start >= after)
				{
					return true;
				}

				std::this_thread::sleep_for(kFramePeriod);
			}

			return false;
		}

		std::vector<std::string> RemoteBranchNames() const
		{
			std::vector<std::string> names;

			for (const Core::Branch& branch : *Git.GetBranches())
			{
				if (branch.Kind == Core::RefKind::Remote)
				{
					names.push_back(branch.Name + (branch.IsRemoteDefault ? " (default)" : ""));
				}
			}

			return names;
		}

		Graphite::RepositoryGitPort Port;
		Graphite::GitModel Git{Port, [this](std::function<void()> job) { m_Reads.Submit(std::move(job)); },
							   [this](std::function<void()> job) { m_Writes.Submit(std::move(job)); }};
		Graphite::NetworkModel Network{Port, Git, [this](std::function<void()> job) { m_NetworkJobs.Submit(std::move(job)); }};

	private:
		// The branch listing asked last has landed with the remotes, and HEAD read since `headBefore`. A HEAD that lands is
		// always a new snapshot.
		bool Listed(const Core::Ref<const Core::HeadState>& headBefore) const
		{
			const bool headAnswered = Git.GetHead() != nullptr && Git.GetHead() != headBefore;
			const bool remotesAnswered = Git.GetRemotes() != nullptr || Git.GetRemotesError().has_value();
			return headAnswered && !Git.IsBranchListingComing() && Git.GetBranches() != nullptr && remotesAnswered;
		}

		std::size_t m_WordsMade = 0;

		static constexpr std::size_t kReadWorkers = 2;
		static constexpr std::size_t kOneWorker = 1;
		Core::JobQueue m_Reads{kReadWorkers};
		Core::JobQueue m_Writes{kOneWorker};
		Core::JobQueue m_NetworkJobs{kOneWorker};
	};

	std::string Joined(const std::vector<std::string>& names)
	{
		std::string text;

		for (const std::string& name : names)
		{
			text += (text.empty() ? "" : ", ") + name;
		}

		return text;
	}

	bool Contains(const std::vector<std::string>& names, const std::string& name)
	{
		for (const std::string& listed : names)
		{
			if (listed == name)
			{
				return true;
			}
		}

		return false;
	}

	// What the window bar's button for `operation` asks of the model when it is clicked.
	void Ask(Graphite::NetworkModel& network, Graphite::NetworkOperation operation)
	{
		// WARNING: No default, so an operation added later is a warning here, and warnings fail the build.
		switch (operation)
		{
		case Graphite::NetworkOperation::Fetch:
			network.Fetch();
			return;
		case Graphite::NetworkOperation::Pull:
			network.Pull();
			return;
		case Graphite::NetworkOperation::Push:
			network.Push();
			return;
		case Graphite::NetworkOperation::FullHistory:
			network.FetchFullHistory();
			return;
		}
	}

	std::string FirstLine(const std::string& text)
	{
		return text.substr(0, text.find('\n'));
	}

	// <root>\large.git: one commit of GRAPHITE_NETWORK_LARGE_MIB files of a MiB of random bytes each, stored and packed
	// without compression, so it is quick to make and slow only to fetch. Made once and kept; <root>\large.ready says it is
	// whole.
	std::filesystem::path LargeServer(const SetupGit& git)
	{
		const std::filesystem::path root = AcceptanceRoot();
		const std::filesystem::path server = root / "large.git";
		const std::filesystem::path ready = root / "large.ready";

		if (std::filesystem::exists(ready))
		{
			MESSAGE("large server kept from before: ", SetupGit::ReadFile(ready), " MiB");
			return server;
		}

		const std::string asked = EnvironmentText("GRAPHITE_NETWORK_LARGE_MIB");
		const int mebibytes = asked.empty() ? kDefaultLargeMebibytes : std::stoi(asked);
		const std::filesystem::path source = root / "large-source";
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

		std::filesystem::remove_all(server);
		std::filesystem::remove_all(source);
		std::filesystem::create_directories(source);
		git.Run(source, {"init", "-q", "--initial-branch=main", "."});

		std::mt19937_64 random{kLargeSeed};
		std::vector<std::uint64_t> block(kMebibyte / sizeof(std::uint64_t));

		for (int file = 0; file < mebibytes; ++file)
		{
			for (std::uint64_t& word : block)
			{
				word = random();
			}

			std::ofstream out{source / ("blob-" + std::to_string(file) + ".bin"), std::ios::binary | std::ios::trunc};
			out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(kMebibyte));
		}

		git.Run(source, {"-c", "core.compression=0", "add", "."});
		git.Run(source, {"-c", "user.name=Acceptance", "-c", "user.email=acceptance@example.com", "commit", "-q", "-m", "large"});
		git.Run(root, {"init", "-q", "--bare", "--initial-branch=main", Core::ToUtf8(server)});
		git.Run(source, {"-c", "pack.compression=0", "push", "-q", Core::ToUtf8(server), kBranch});
		std::filesystem::remove_all(source);
		SetupGit::WriteFile(ready, std::to_string(mebibytes));

		MESSAGE("large server made: ", mebibytes, " MiB in ", Milliseconds(std::chrono::steady_clock::now() - start), " ms");
		return server;
	}

	// git's own progress, as the runner hands it on: every call, and every change of the percent.
	struct ProgressCount
	{
		std::atomic<std::size_t> Calls{0};
		std::atomic<std::size_t> Changes{0};
		std::atomic<int> Last{-1};
		std::atomic<bool> Counting{false};

		void Note(int percent)
		{
			Calls.fetch_add(1);
			Counting.store(true);

			if (Last.exchange(percent) != percent)
			{
				Changes.fetch_add(1);
			}
		}
	};

	// Whether every process git started has let go of the folder: none has it as its current folder, nor holds a file in
	// it open, once it can be renamed. Gives it back its name.
	bool FolderLetGo(const std::filesystem::path& folder)
	{
		const std::filesystem::path moved = folder.parent_path() / (folder.filename().native() + L"-moved");
		std::error_code renameError;
		std::filesystem::rename(folder, moved, renameError);

		if (renameError)
		{
			return false;
		}

		std::filesystem::rename(moved, folder, renameError);
		return !renameError;
	}

	// What a fetch's index-pack left in objects/pack under a temporary name, as a cancel can leave it, with its size.
	std::string TemporaryPackFiles(const std::filesystem::path& repository)
	{
		std::string found;

		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{repository / ".git" / "objects" / "pack"})
		{
			const std::string name = Core::ToUtf8(entry.path().filename());

			if (name.starts_with("tmp_"))
			{
				found += (found.empty() ? "" : ", ") + name + " (" + std::to_string(entry.file_size() / kMebibyte) + " MiB)";
			}
		}

		return found.empty() ? "nothing" : found;
	}
}

TEST_CASE("Network acceptance: fetch brings a branch made on the server and prunes one deleted there" * doctest::skip())
{
	const Scene scene{"fetch"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	scene.Git.Run(peer, {"push", "-q", kRemote, "main:old-topic"});
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");
	REQUIRE(scene.Git.HasRef(work, "refs/remotes/origin/old-topic"));

	scene.Git.Run(peer, {"push", "-q", kRemote, "main:nowa-gałąź"});
	scene.Git.Run(peer, {"push", "-q", kRemote, "--delete", "old-topic"});

	AppNetwork app{work, scene.Store()};
	app.List();
	const std::vector<std::string> before = app.RemoteBranchNames();
	MESSAGE("remote branches before: ", Joined(before));

	app.Network.Fetch();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing), " in ", landing.Milliseconds, " ms");
	REQUIRE(landing.Done.has_value());
	CHECK(Graphite::DoneText(*landing.Done) == "Fetched");

	app.List();
	const std::vector<std::string> after = app.RemoteBranchNames();
	MESSAGE("remote branches after: ", Joined(after));

	CHECK(Contains(before, "origin/old-topic"));
	CHECK_FALSE(Contains(before, "origin/nowa-gałąź"));
	CHECK(Contains(after, "origin/nowa-gałąź"));
	CHECK_FALSE(Contains(after, "origin/old-topic"));
	CHECK_FALSE(scene.Git.HasRef(work, "refs/remotes/origin/old-topic"));
	CHECK(app.Git.GetLastFetch().has_value());
}

TEST_CASE("Network acceptance: a fetch records the remote's default on a clone made without one" * doctest::skip())
{
	const Scene scene{"remote-default"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	scene.Git.Run(peer, {"push", "-q", kRemote, "main:release"});

	// As git before 2.48 and a clone made by libgit2 leave it: the remote's branches fetched, no refs/remotes/origin/HEAD.
	const std::filesystem::path work = scene.EmptyClone(server, "work ż");
	scene.Git.Run(work, {"config", "remote.origin.followRemoteHEAD", "never"});
	scene.Git.Run(work, {"fetch", "-q", kRemote});
	REQUIRE(scene.Git.HasRef(work, "refs/remotes/origin/main"));
	REQUIRE(scene.Git.RunAnyway(work, {"symbolic-ref", "-q", "refs/remotes/origin/HEAD"}).ExitCode != 0);

	AppNetwork app{work, scene.Store()};
	app.List();
	MESSAGE("remote branches before: ", Joined(app.RemoteBranchNames()));

	app.Network.Fetch();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));
	REQUIRE(landing.Done.has_value());

	app.List();
	MESSAGE("remote branches after: ", Joined(app.RemoteBranchNames()));
	CHECK(scene.Git.Run(work, {"symbolic-ref", "refs/remotes/origin/HEAD"}) == "refs/remotes/origin/main");
	CHECK(Contains(app.RemoteBranchNames(), "origin/main (default)"));
	CHECK(Contains(app.RemoteBranchNames(), "origin/release"));
}

TEST_CASE("Network acceptance: pull fast-forwards the branch to what the server gained" * doctest::skip())
{
	const Scene scene{"pull-fast-forward"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	for (const char* file : {"b.txt", "c.txt", "d.txt"})
	{
		scene.Git.CommitFile(peer, file, std::string{file} + "\n");
	}

	scene.Git.Run(peer, {"push", "-q", kRemote, kBranch});

	AppNetwork app{work, scene.Store()};
	app.List();
	app.Network.Pull();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));

	REQUIRE(landing.Done.has_value());
	CHECK(landing.Done->Commits == std::optional<std::size_t>{3});
	CHECK(Graphite::DoneText(*landing.Done) == "Pulled 3 commits");
	CHECK(scene.Git.Head(work) == scene.Git.Head(peer));
	CHECK(std::filesystem::exists(work / "d.txt"));
	// As the app sees the work tree, with the user's own line-ending config the merge checked the files out with.
	const Core::Result<Core::WorkingTreeStatus> status = app.Port.QueryStatus();
	REQUIRE(status.HasValue());
	CHECK(status.GetValue().Entries.empty());
}

TEST_CASE("Network acceptance: pull refuses a branch that has diverged and merges nothing" * doctest::skip())
{
	const Scene scene{"pull-diverged"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	scene.Git.CommitFile(work, "mine.txt", "mine\n");
	scene.Git.CommitFile(peer, "theirs-1.txt", "one\n");
	scene.Git.CommitFile(peer, "theirs-2.txt", "two\n");
	scene.Git.Run(peer, {"push", "-q", kRemote, kBranch});
	const std::string headBefore = scene.Git.Head(work);

	AppNetwork app{work, scene.Store()};
	app.List();
	app.Network.Pull();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));

	REQUIRE(landing.Failure.has_value());
	CHECK(Graphite::FailureTitle(landing.Failure->Operation) == "Pull failed");
	CHECK(landing.Failure->Error.Message == Graphite::DivergedText(kBranch, "origin/main", 1, 2));
	CHECK(scene.Git.Head(work) == headBefore);
	CHECK_FALSE(scene.Git.HasRef(work, "MERGE_HEAD"));

	// Its own fetch ran: the upstream is the server's.
	CHECK(scene.Git.Run(work, {"rev-parse", "refs/remotes/origin/main"}) == scene.Git.Head(peer));
}

TEST_CASE("Network acceptance: pull refuses to overwrite local changes and names the files" * doctest::skip())
{
	const Scene scene{"pull-local-changes"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	scene.Git.CommitFile(peer, "a.txt", "theirs\n");
	scene.Git.Run(peer, {"push", "-q", kRemote, kBranch});
	SetupGit::WriteFile(work / "a.txt", "mine, not committed\n");
	const std::string headBefore = scene.Git.Head(work);

	AppNetwork app{work, scene.Store()};
	app.List();
	app.Network.Pull();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));
	MESSAGE("copied: ", landing.Failure.has_value() ? landing.Failure->Copied : std::string{});

	REQUIRE(landing.Failure.has_value());
	CHECK(Graphite::FailureTitle(landing.Failure->Operation) == "Pull failed");
	CHECK(landing.Failure->Error.Message.starts_with("Commit or stash your changes to these files first:"));
	CHECK(landing.Failure->Error.Message.find("a.txt") != std::string::npos);
	CHECK(landing.Failure->Copied.find("would be overwritten by merge") != std::string::npos);
	CHECK(scene.Git.Head(work) == headBefore);
	CHECK(SetupGit::ReadFile(work / "a.txt") == "mine, not committed\n");
}

TEST_CASE("Network acceptance: push sends the branch's commits to its upstream" * doctest::skip())
{
	const Scene scene{"push"};
	const std::filesystem::path server = scene.Server("server");
	scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	scene.Git.CommitFile(work, "mine-1.txt", "one\n");
	scene.Git.CommitFile(work, "mine-2.txt", "two\n");

	AppNetwork app{work, scene.Store()};
	app.List();
	const Graphite::NetworkButtons buttons = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false);
	MESSAGE("push button: [", buttons.Push.Label, "] count [", buttons.Push.Count, "] tooltip [", buttons.Push.Tooltip, "]");
	CHECK(buttons.Push.Count == "2");
	CHECK(buttons.Push.Tooltip == "Push 2 commits to origin");

	app.Network.Push();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));

	REQUIRE(landing.Done.has_value());
	CHECK(landing.Done->Commits == std::optional<std::size_t>{2});
	CHECK(Graphite::DoneText(*landing.Done) == "Pushed 2 commits");
	CHECK(scene.Git.Run(server, {"rev-parse", kBranch}) == scene.Git.Head(work));
}

TEST_CASE("Network acceptance: push with no upstream and two remotes asks which, and sets the one chosen as upstream" * doctest::skip())
{
	const Scene scene{"push-two-remotes"};
	const std::filesystem::path origin = scene.Server("server");
	const std::filesystem::path backup = scene.Server("backup");
	scene.Peer(origin, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(origin), "work ż");
	scene.Git.Run(work, {"remote", "add", "backup", Core::ToUtf8(backup)});
	scene.Git.Run(work, {"checkout", "-q", "-b", "feature"});
	scene.Git.CommitFile(work, "feature.txt", "feature\n");

	AppNetwork app{work, scene.Store()};
	app.List();
	const Graphite::NetworkButtons asking = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false);
	MESSAGE("push button: [", asking.Push.Label, "] tooltip [", asking.Push.Tooltip, "] asks ", asking.Push.AsksWhichRemote);
	CHECK(asking.Push.Enabled);
	CHECK(asking.Push.AsksWhichRemote);
	CHECK(asking.Push.Tooltip == "Push feature to…");
	CHECK_FALSE(Graphite::CommandStateOfPush(asking).Enabled);

	// The menu's second remote, as a click on it asks.
	app.Network.Push(std::string{"backup"});
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));

	REQUIRE(landing.Done.has_value());
	CHECK(scene.Git.Run(backup, {"rev-parse", "refs/heads/feature"}) == scene.Git.Head(work));
	CHECK_FALSE(scene.Git.HasRef(origin, "refs/heads/feature"));
	CHECK(scene.Git.Run(work, {"config", "branch.feature.remote"}) == "backup");
	CHECK(scene.Git.Run(work, {"config", "branch.feature.merge"}) == "refs/heads/feature");

	app.List();
	const Graphite::NetworkButtons after = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false);
	MESSAGE("push button after: [", after.Push.Label, "] tooltip [", after.Push.Tooltip, "]");
	CHECK_FALSE(after.Push.AsksWhichRemote);
}

TEST_CASE("Network acceptance: a push the server rejects says to pull first, with git's words to copy" * doctest::skip())
{
	const Scene scene{"push-rejected"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	scene.Git.CommitFile(peer, "theirs.txt", "theirs\n");
	scene.Git.Run(peer, {"push", "-q", kRemote, kBranch});
	scene.Git.CommitFile(work, "mine.txt", "mine\n");

	AppNetwork app{work, scene.Store()};
	app.List();
	app.Network.Push();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));
	MESSAGE("copied: ", landing.Failure.has_value() ? landing.Failure->Copied : std::string{});

	REQUIRE(landing.Failure.has_value());
	CHECK(Graphite::FailureTitle(landing.Failure->Operation) == "Push failed");
	CHECK(FirstLine(landing.Failure->Error.Message) == "origin has commits that main lacks. Pull first, or merge in a terminal.");
	CHECK(landing.Failure->Copied.find("[rejected]") != std::string::npos);
	CHECK(scene.Git.Run(server, {"rev-parse", kBranch}) == scene.Git.Head(peer));
}

TEST_CASE("Network acceptance: a remote at a path that does not exist fails each operation with git's words" * doctest::skip())
{
	const Scene scene{"unreachable"};
	const std::filesystem::path server = scene.Server("server");
	scene.Peer(server, "peer");
	const std::filesystem::path work = scene.Clone(Core::ToUtf8(server), "work ż");

	// A fetch that ended well two hours ago.
	scene.Git.Run(work, {"fetch", "-q", kRemote});
	const std::filesystem::path fetchHead = work / ".git" / "FETCH_HEAD";
	std::filesystem::last_write_time(fetchHead, std::filesystem::file_time_type::clock::now() - std::chrono::hours{2});

	scene.Git.Run(work, {"remote", "set-url", kRemote, Core::ToUtf8(scene.Folder() / "nowhere.git")});
	scene.Git.CommitFile(work, "mine.txt", "mine\n");

	AppNetwork app{work, scene.Store()};
	app.List();
	const std::string ageBefore = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false).FetchedAge;
	MESSAGE("before: ", ageBefore);

	for (const Graphite::NetworkOperation operation :
		 {Graphite::NetworkOperation::Fetch, Graphite::NetworkOperation::Pull, Graphite::NetworkOperation::Push})
	{
		Ask(app.Network, operation);
		const Landing landing = app.RunToEnd();
		MESSAGE(Describe(landing));

		REQUIRE(landing.Failure.has_value());
		CHECK(landing.Failure->Operation == operation);
		CHECK(landing.Failure->Error.Message.find("does not appear to be a git repository") != std::string::npos);
		CHECK(landing.Failure->Copied.find("Could not read from remote repository") != std::string::npos);

		// git wrote FETCH_HEAD before it found no repository there; the bar keeps the age of the fetch that ended well.
		app.List();
		const std::string age = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false).FetchedAge;
		MESSAGE("after it: ", age);
		CHECK(age == ageBefore);
	}

	// FETCH_HEAD's time changing again, as a fetch in a terminal changes it, is shown.
	std::filesystem::last_write_time(fetchHead, std::filesystem::file_time_type::clock::now() - std::chrono::minutes{30});
	app.List();
	const std::string ageAfterTerminal = Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false).FetchedAge;
	MESSAGE("after a terminal's fetch: ", ageAfterTerminal);
	CHECK(ageAfterTerminal != ageBefore);
	CHECK_FALSE(ageAfterTerminal.empty());
}

TEST_CASE("Network acceptance: fetch full history deepens a clone made with --depth 5" * doctest::skip())
{
	constexpr int kCommits = 20;
	constexpr char kDepth[] = "5";

	const Scene scene{"full-history"};
	const std::filesystem::path server = scene.Server("server");
	const std::filesystem::path peer = scene.Peer(server, "peer");

	for (int commit = 1; commit < kCommits; ++commit)
	{
		scene.Git.CommitFile(peer, "a.txt", "change " + std::to_string(commit) + "\n");
	}

	scene.Git.Run(peer, {"push", "-q", kRemote, kBranch});

	// A local path ignores --depth; a file:// URL is fetched as a server would be.
	const std::filesystem::path work = scene.Clone("file:///" + ForwardSlashes(server), "shallow ż", {"--depth", kDepth});
	REQUIRE(scene.Git.Run(work, {"rev-list", "--count", "HEAD"}) == kDepth);
	REQUIRE(scene.Git.Run(work, {"rev-parse", "--is-shallow-repository"}) == "true");

	AppNetwork app{work, scene.Store()};
	app.List();
	app.Network.FetchFullHistory();
	const Landing landing = app.RunToEnd();
	MESSAGE(Describe(landing));

	REQUIRE(landing.Done.has_value());
	CHECK(landing.Done->Operation == Graphite::NetworkOperation::FullHistory);
	CHECK(scene.Git.Run(work, {"rev-list", "--count", "HEAD"}) == std::to_string(kCommits));
	CHECK(scene.Git.Run(work, {"rev-parse", "--is-shallow-repository"}) == "false");
}

TEST_CASE("Network acceptance: cancel during a fetch made slow by a large repository" * doctest::skip())
{
	const Scene scene{"cancel"};
	const std::filesystem::path large = LargeServer(scene.Git);

	SUBCASE("through the model, as the button cancels")
	{
		const std::filesystem::path work = scene.EmptyClone(large, "model ż");
		AppNetwork app{work, scene.Store()};
		app.List();
		app.Network.Fetch();
		REQUIRE(app.RunUntilCounting(kRunBeforeCancel));

		const Graphite::NetworkState running = app.Network.GetState();
		MESSAGE("cancelled at ", running.Percent.value_or(-1), "%, ", Graphite::RunningText(running));
		REQUIRE(running.Cancellable);

		const std::chrono::steady_clock::time_point cancelledAt = std::chrono::steady_clock::now();
		app.Network.Cancel(running.Serial);

		std::optional<Graphite::NetworkDone> done;

		while (!done.has_value() && std::chrono::steady_clock::now() - cancelledAt < kPatience)
		{
			std::this_thread::sleep_for(kCancelPollPeriod);
			app.Frame();
			done = app.Network.TakeDone();
			REQUIRE_FALSE(app.Network.TakeFailure().has_value());
		}

		const double cancelMilliseconds = Milliseconds(std::chrono::steady_clock::now() - cancelledAt);
		MESSAGE("model: Cancel to the operation's end ", cancelMilliseconds, " ms");

		REQUIRE(done.has_value());
		CHECK(done->Cancelled);
		CHECK(Graphite::DoneText(*done) == "Fetch cancelled");
		CHECK_FALSE(app.Network.GetState().Running.has_value());
		CHECK(cancelMilliseconds < kMostCancelMilliseconds);
		MESSAGE("model: left in objects/pack under a temporary name: ", TemporaryPackFiles(work));

		// git wrote FETCH_HEAD before it was cancelled; a clone never fetched still shows no age.
		app.List();
		CHECK(app.Git.GetLastFetch().has_value());
		CHECK(Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false).FetchedAge.empty());

		// The next fetch of the same clone runs to its end, and its age is shown.
		app.Network.Fetch();
		const Landing next = app.RunToEnd();
		MESSAGE("the next fetch: ", Describe(next), " in ", next.Milliseconds, " ms");
		REQUIRE(next.Done.has_value());
		CHECK_FALSE(next.Done->Cancelled);
		CHECK(scene.Git.HasRef(work, "refs/remotes/origin/main"));
		app.List();
		CHECK(Graphite::DescribeNetworkButtonsNow(app.Git, app.Network, false).FetchedAge == "fetched just now");
	}

	SUBCASE("through the port, and every process git started ends")
	{
		const std::filesystem::path work = scene.EmptyClone(large, "port ż");
		std::atomic<bool> cancelled{false};
		ProgressCount progress;
		double cancelMilliseconds = 0.0;
		Core::NetworkOutcome outcome;

		{
			Graphite::RepositoryGitPort port{work, scene.Store()};
			const Core::ProgressSink sink = [&progress](int percent) { progress.Note(percent); };
			std::future<Core::NetworkOutcome> fetching = std::async(std::launch::async, &Graphite::RepositoryGitPort::FetchAll, &port,
																	std::cref(cancelled), std::cref(sink));

			const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			bool countedLongEnough = false;

			while (!countedLongEnough && fetching.wait_for(kFramePeriod) != std::future_status::ready)
			{
				countedLongEnough = progress.Counting.load() && std::chrono::steady_clock::now() - start >= kRunBeforeCancel;
			}

			REQUIRE(countedLongEnough);

			const std::chrono::steady_clock::time_point cancelledAt = std::chrono::steady_clock::now();
			cancelled.store(true);
			outcome = fetching.get();
			cancelMilliseconds = Milliseconds(std::chrono::steady_clock::now() - cancelledAt);
		}

		MESSAGE("port: cancel to FetchAll's return ", cancelMilliseconds, " ms, at ", progress.Last.load(), "%");
		CHECK(outcome.Result == Core::NetworkResult::Cancelled);
		CHECK(cancelMilliseconds < kMostCancelMilliseconds);

		const std::chrono::steady_clock::time_point letGoFrom = std::chrono::steady_clock::now();
		const bool letGoAtOnce = FolderLetGo(work);
		MESSAGE("the folder let go at once: ", letGoAtOnce);
		CHECK(letGoAtOnce);
		MESSAGE("checked in ", Milliseconds(std::chrono::steady_clock::now() - letGoFrom), " ms");
		MESSAGE("port: left in objects/pack under a temporary name: ", TemporaryPackFiles(work));
	}
}

TEST_CASE("Network acceptance: what the UI thread's frames cost and how often git counts while a large fetch runs" * doctest::skip())
{
	const Scene scene{"timings"};
	const std::filesystem::path large = LargeServer(scene.Git);

	// git's own progress through the port alone, over the whole fetch.
	{
		const std::filesystem::path work = scene.EmptyClone(large, "progress ż");
		Graphite::RepositoryGitPort port{work, scene.Store()};
		ProgressCount progress;
		const Core::ProgressSink sink = [&progress](int percent) { progress.Note(percent); };
		const std::atomic<bool> neverCancelled{false};

		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		const Core::NetworkOutcome outcome = port.FetchAll(neverCancelled, sink);
		const double seconds = Milliseconds(std::chrono::steady_clock::now() - start) / 1000.0;

		REQUIRE_MESSAGE(outcome.Result == Core::NetworkResult::Done, outcome.Failure.Copied);
		MESSAGE("port: fetch of the large server in ", seconds, " s; git's percent lines ", progress.Calls.load(), " (",
				static_cast<double>(progress.Calls.load()) / seconds, " a second), changes of the percent ", progress.Changes.load(),
				" (", static_cast<double>(progress.Changes.load()) / seconds, " a second)");
	}

	// The frames, idle and then while the same fetch runs, at 60 a second.
	{
		const std::filesystem::path work = scene.EmptyClone(large, "frames ż");
		AppNetwork app{work, scene.Store()};
		app.List();

		FrameCosts idle;

		for (int frame = 0; frame < kIdleFrames; ++frame)
		{
			idle.Milliseconds.push_back(app.Frame());
			std::this_thread::sleep_for(kFramePeriod);
		}

		app.Network.Fetch();
		FrameCosts running;
		const Landing landing = app.RunToEnd(kFramePeriod, &running);
		REQUIRE(landing.Done.has_value());

		MESSAGE("frames idle: ", Summary(idle));
		MESSAGE("frames while the fetch ran: ", Summary(running), "; ", running.PercentChanges, " frames saw a new percent (",
				static_cast<double>(running.PercentChanges) / running.Seconds, " a second) over ", running.Seconds, " s");
		MESSAGE("beyond idle: mean ", (running.Mean() - idle.Mean()) * 1000.0, " us, p99 ",
				(running.Quantile(0.99) - idle.Quantile(0.99)) * 1000.0, " us");

		CHECK(running.Mean() - idle.Mean() < kMostFrameCostBeyondIdleMilliseconds);
		CHECK(running.Quantile(0.99) - idle.Quantile(0.99) < kMostFrameCostBeyondIdleMilliseconds);
	}
}
