#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "GitFixtures.h"
#include "Git/Branches/BranchLookup.h"
#include "Git/GitModel.h"
#include "Git/Network/NetworkModel.h"
#include "Git/Network/NetworkText.h"

#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Jobs/JobQueue.h"
#include "Workspace/Workspace.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Graphite::NetworkDone;
using Graphite::NetworkFailureReport;
using Graphite::NetworkModel;
using Graphite::NetworkOperation;
using Graphite::NetworkState;

namespace {

	constexpr float kFrameSeconds = 0.016f;
	constexpr std::chrono::seconds kPatience{5};
	constexpr std::chrono::milliseconds kPoll{1};

	// Long enough for the held call to have polled its cancel flag many times.
	constexpr std::chrono::milliseconds kPollsToSettle{50};
	constexpr std::chrono::milliseconds kDetachAllowance{2000};
	constexpr int kHeldPercent = 45;

	Core::Branch LocalBranch(std::string name, std::optional<std::string> upstream = std::nullopt, std::size_t ahead = 0,
							 std::size_t behind = 0)
	{
		Core::Branch branch;
		branch.FullName = "refs/heads/" + name;
		branch.Name = std::move(name);
		branch.Kind = Core::RefKind::Local;
		branch.Upstream = std::move(upstream);
		branch.Ahead = ahead;
		branch.Behind = behind;
		branch.DistanceKnown = branch.Upstream.has_value();
		return branch;
	}

	Core::Branch CheckedOut(Core::Branch branch)
	{
		branch.IsHead = true;
		return branch;
	}

	Core::Branch RemoteBranch(const std::string& remote, const std::string& name, bool remoteDefault = false)
	{
		Core::Branch branch;
		branch.Name = remote + "/" + name;
		branch.FullName = "refs/remotes/" + remote + "/" + name;
		branch.Kind = Core::RefKind::Remote;
		branch.IsRemoteDefault = remoteDefault;
		return branch;
	}

	Core::NetworkOutcome Failed(std::string shown, std::string copied, Core::NetworkHint hint = Core::NetworkHint::None)
	{
		return Core::NetworkOutcome{.Result = Core::NetworkResult::Failed,
									.Failure = {.Shown = std::move(shown), .Copied = std::move(copied), .Hint = hint}};
	}

	// A repository whose branches, remotes and network answers a test sets. Each network call is recorded by its name and
	// arguments and answered from Outcomes by its name alone, Done when unset. The call named Held reports its percent and
	// then waits, as git does, until the test releases it or the cancel flag is set. Its jobs may run on a worker.
	class NetworkPort final : public EmptyGitPort
	{
	public:
		std::vector<Core::Branch> Branches;
		std::vector<Core::Remote> Remotes{Core::Remote{.Name = "origin"}};
		std::map<std::string, Core::NetworkOutcome> Outcomes;

		// What the branches read once a fetch of one remote has brought the remote's new commits; unchanged when unset.
		std::optional<std::vector<Core::Branch>> BranchesAfterFetch;

		std::filesystem::path WorkingDirectory{"C:/repo"};

		// When set, the working directory cannot be read.
		std::optional<Core::Error> WorkingDirectoryFailure;

		// A commit moves the checked-out branch's tip to this, one more commit ahead.
		Core::Oid CommitTip = OidWithFirstByte(0xC0);

		// The checked-out branch's tip at each push, in order.
		std::vector<Core::Oid> PushedTips;

		// FETCH_HEAD's time once a fetch has started: git writes the file before it reaches the remote, so a fetch that then
		// fails or is cancelled has written it too. Unchanged when unset.
		std::optional<std::chrono::sys_seconds> FetchHeadWrittenAt;

		std::string Held;
		std::atomic<bool> HeldEntered{false};
		std::atomic<bool> HeldReleased{false};
		std::atomic<bool> HeldSawCancel{false};

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return Branches; }
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return Remotes; }
		Core::Result<std::filesystem::path> GetWorkingDirectory() override
		{
			if (WorkingDirectoryFailure.has_value())
			{
				return *WorkingDirectoryFailure;
			}

			return WorkingDirectory;
		}

		Core::Result<Core::Oid> Commit(const Core::CommitRequest&) override
		{
			for (Core::Branch& branch : Branches)
			{
				if (branch.IsHead)
				{
					branch.Tip = CommitTip;
					++branch.Ahead;
				}
			}

			return CommitTip;
		}

		Core::NetworkOutcome FetchAll(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress) override
		{
			return Answer("FetchAll", "FetchAll", cancelled, progress);
		}

		Core::NetworkOutcome FetchRemote(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
										 const std::string& remote) override
		{
			const Core::NetworkOutcome outcome = Answer("FetchRemote", "FetchRemote " + remote, cancelled, progress);

			if (BranchesAfterFetch.has_value())
			{
				Branches = *BranchesAfterFetch;
			}

			return outcome;
		}

		Core::NetworkOutcome FetchFullHistory(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress) override
		{
			return Answer("FetchFullHistory", "FetchFullHistory", cancelled, progress);
		}

		Core::NetworkOutcome FastForward(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
										 const std::string& upstream) override
		{
			return Answer("FastForward", "FastForward " + upstream, cancelled, progress);
		}

		Core::NetworkOutcome PushTo(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress, const std::string& remote,
									const std::optional<std::string>& upstreamBranch) override
		{
			if (const Core::Branch* checkedOut = Graphite::CheckedOutBranch(Branches); checkedOut != nullptr)
			{
				PushedTips.push_back(checkedOut->Tip);
			}

			return Answer("PushTo", "PushTo " + remote + " " + upstreamBranch.value_or("(set upstream)"), cancelled, progress);
		}

		Core::NetworkOutcome SetRemoteHeadAuto(const std::atomic<bool>& cancelled, const Core::ProgressSink& progress,
											   const std::string& remote) override
		{
			return Answer("SetRemoteHeadAuto", "SetRemoteHeadAuto " + remote, cancelled, progress);
		}

		std::vector<std::string> CallsSoFar()
		{
			const std::lock_guard<std::mutex> callsLock{m_CallsMutex};
			return m_Calls;
		}

		bool WaitUntilHeldEntered() const
		{
			const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kPatience;

			while (!HeldEntered.load())
			{
				if (std::chrono::steady_clock::now() > deadline)
				{
					return false;
				}

				std::this_thread::sleep_for(kPoll);
			}

			return true;
		}

	private:
		Core::NetworkOutcome Answer(const std::string& name, std::string call, const std::atomic<bool>& cancelled,
									const Core::ProgressSink& progress)
		{
			{
				const std::lock_guard<std::mutex> callsLock{m_CallsMutex};
				m_Calls.push_back(std::move(call));
			}

			const bool fetches = name.starts_with("Fetch");

			if (fetches && FetchHeadWrittenAt.has_value())
			{
				Network.LastFetch = FetchHeadWrittenAt;
			}

			const std::map<std::string, Core::NetworkOutcome>::const_iterator set = Outcomes.find(name);
			const Core::NetworkOutcome outcome = set == Outcomes.end() ? Core::NetworkOutcome{} : set->second;

			if (name != Held)
			{
				return outcome;
			}

			if (progress)
			{
				progress(kHeldPercent);
			}

			HeldEntered.store(true);
			return WaitAsGitDoes(cancelled, outcome);
		}

		Core::NetworkOutcome WaitAsGitDoes(const std::atomic<bool>& cancelled, const Core::NetworkOutcome& outcome)
		{
			while (!HeldReleased.load())
			{
				if (cancelled.load())
				{
					HeldSawCancel.store(true);
					return Core::NetworkOutcome{.Result = Core::NetworkResult::Cancelled};
				}

				std::this_thread::sleep_for(kPoll);
			}

			return outcome;
		}

		std::mutex m_CallsMutex;
		std::vector<std::string> m_Calls;
	};

	// Every job answered where it was asked: the network model's, the git model's reads and its writes.
	struct InlineNetwork
	{
		NetworkPort Port;
		Graphite::GitModel Git{Port, RunJobsInline()};
		NetworkModel Model{Port, Git, RunJobsInline()};

		InlineNetwork() { Git.SetRoot("C:/repo"); }

		void Update()
		{
			Git.OnUpdate(Core::Timestep{kFrameSeconds});
			Model.OnUpdate(Core::Timestep{kFrameSeconds});
		}
	};

	// The network model's jobs on a worker of their own, as in the app; the git model's inline.
	// WARNING: The queue is declared last, so it joins its worker before the model and the port go.
	struct ThreadedNetwork
	{
		NetworkPort Port;
		Graphite::GitModel Git{Port, RunJobsInline()};
		NetworkModel Model{Port, Git, [this](std::function<void()> job) { Jobs.Submit(std::move(job)); }};
		Core::JobQueue Jobs{1};

		ThreadedNetwork() { Git.SetRoot("C:/repo"); }

		// A test that failed while a call was held must not leave the worker waiting for ever.
		~ThreadedNetwork() { Model.Detach(); }

		std::optional<NetworkDone> WaitForDone()
		{
			const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kPatience;

			while (std::chrono::steady_clock::now() < deadline)
			{
				Git.OnUpdate(Core::Timestep{kFrameSeconds});
				Model.OnUpdate(Core::Timestep{kFrameSeconds});

				if (std::optional<NetworkDone> done = Model.TakeDone(); done.has_value())
				{
					return done;
				}

				std::this_thread::sleep_for(kPoll);
			}

			return std::nullopt;
		}
	};

	std::vector<std::string> Calls(std::initializer_list<const char*> names)
	{
		std::vector<std::string> calls;

		for (const char* name : names)
		{
			calls.emplace_back(name);
		}

		return calls;
	}
}

TEST_CASE("Network model: one operation runs at a time and the others wait")
{
	ThreadedNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 2, 0)), RemoteBranch("origin", "main", true)};
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());

	network.Model.Push();
	network.Model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(network.Model.GetState().Running == std::optional<NetworkOperation>{NetworkOperation::Fetch});
	CHECK(network.Port.CallsSoFar() == Calls({"FetchAll"}));

	network.Port.HeldReleased.store(true);

	const std::optional<NetworkDone> fetched = network.WaitForDone();
	REQUIRE(fetched.has_value());
	CHECK(fetched->Operation == NetworkOperation::Fetch);

	const std::optional<NetworkDone> pushed = network.WaitForDone();
	REQUIRE(pushed.has_value());
	CHECK(pushed->Operation == NetworkOperation::Push);
	CHECK(pushed->Commits == std::optional<std::size_t>{2});

	CHECK(network.Port.CallsSoFar() == Calls({"FetchAll", "PushTo origin main"}));
	CHECK_FALSE(network.Model.GetState().Running.has_value());
}

TEST_CASE("Network model: cancel ends the running operation and reports it cancelled, not failed")
{
	ThreadedNetwork network;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());

	CHECK(network.Model.GetState().Cancellable);

	network.Model.Cancel(network.Model.GetState().Serial);

	const std::optional<NetworkDone> done = network.WaitForDone();
	REQUIRE(done.has_value());
	CHECK(*done == NetworkDone{.Operation = NetworkOperation::Fetch, .Cancelled = true});
	CHECK(network.Port.HeldSawCancel.load());
	CHECK_FALSE(network.Model.TakeFailure().has_value());
	CHECK(network.Model.GetState() == NetworkState{});
	CHECK(network.Model.GetShownDone() == std::optional<NetworkDone>{*done});
}

TEST_CASE("Network model: a cancel that comes after git has finished takes git's outcome")
{
	DeferredJobs jobs;
	NetworkPort port;
	Graphite::GitModel git{port, RunJobsInline()};
	NetworkModel model{port, git, jobs.Submitter()};
	git.SetRoot("C:/repo");

	model.Fetch();
	jobs.RunAll();
	model.Cancel(model.GetState().Serial);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
}

TEST_CASE("Network model: a cancel that comes after pull's fetch has finished stops it before its merge")
{
	DeferredJobs jobs;
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 1)), RemoteBranch("origin", "main")};
	Graphite::GitModel git{port, RunJobsInline()};
	NetworkModel model{port, git, jobs.Submitter()};
	git.SetRoot("C:/repo");

	model.Pull();
	jobs.RunAll();
	model.Cancel(model.GetState().Serial);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Pull, .Cancelled = true}});
	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin"}));
	CHECK_FALSE(git.IsWriteUnsettled());
}

TEST_CASE("Network model: progress percent reaches the state")
{
	ThreadedNetwork network;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());

	const NetworkState state = network.Model.GetState();

	CHECK(state.Running == std::optional<NetworkOperation>{NetworkOperation::Fetch});
	CHECK(state.Percent == std::optional<int>{kHeldPercent});

	network.Port.HeldReleased.store(true);
	REQUIRE(network.WaitForDone().has_value());

	CHECK_FALSE(network.Model.GetState().Percent.has_value());
}

TEST_CASE("Network model: a failure keeps git's words and the hint first")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 1, 0)), RemoteBranch("origin", "main")};

	const std::string shown = "The remote has commits that this branch lacks. Pull first, or merge in a terminal.\n"
							  " ! [rejected]        main -> main (fetch first)\n"
							  "error: failed to push some refs to 'C:/server.git'";
	const std::string copied = "To C:/server.git\n"
							   " ! [rejected]        main -> main (fetch first)\n"
							   "error: failed to push some refs to 'C:/server.git'\n"
							   "hint: Updates were rejected because the remote contains work that you do not\n";
	network.Port.Outcomes["PushTo"] = Failed(shown, copied, Core::NetworkHint::Rejected);

	network.Model.Push();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Operation == NetworkOperation::Push);
	CHECK(failure->Error.Message == "origin has commits that main lacks. Pull first, or merge in a terminal.\n"
									" ! [rejected]        main -> main (fetch first)\n"
									"error: failed to push some refs to 'C:/server.git'");
	CHECK(failure->Copied == copied);
	CHECK_FALSE(network.Model.TakeDone().has_value());
	CHECK_FALSE(network.Model.GetShownDone().has_value());
}

TEST_CASE("Network model: after a fetch a remote without a default branch gets one")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "origin"}, Core::Remote{.Name = "upstream"}, Core::Remote{.Name = "origin/mirror"}};
	network.Port.Branches = {RemoteBranch("origin", "main", true), RemoteBranch("upstream", "main"),
							 RemoteBranch("origin/mirror", "trunk", true)};

	network.Model.Fetch();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"FetchAll", "SetRemoteHeadAuto upstream"}));
	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
}

TEST_CASE("Network model: a fetch that failed records no default branch")
{
	InlineNetwork network;
	network.Port.Branches = {RemoteBranch("origin", "main")};
	network.Port.Outcomes["FetchAll"] = Failed("fatal: unable to access", "fatal: unable to access\n");

	network.Model.Fetch();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"FetchAll"}));
	REQUIRE(network.Model.TakeFailure().has_value());
}

TEST_CASE("Network model: pull counts the commits its own fetch brought")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 0)), RemoteBranch("origin", "main")};
	network.Port.BranchesAfterFetch =
		std::vector<Core::Branch>{CheckedOut(LocalBranch("main", "origin/main", 0, 5)), RemoteBranch("origin", "main")};

	network.Model.Pull();
	network.Update();
	network.Update();

	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 5}});
	CHECK(network.Port.CallsSoFar() == Calls({"FetchRemote origin", "FastForward refs/remotes/origin/main"}));
}

TEST_CASE("Network model: pull finds the branch diverged by the commits its own fetch brought")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 3, 0)), RemoteBranch("origin", "main")};
	network.Port.BranchesAfterFetch =
		std::vector<Core::Branch>{CheckedOut(LocalBranch("main", "origin/main", 3, 5)), RemoteBranch("origin", "main")};

	network.Model.Pull();
	network.Update();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message ==
		  "main and origin/main have diverged (3 to push, 5 to pull). Pull can only fast-forward for now; merge or rebase in a "
		  "terminal.");
	CHECK(network.Port.CallsSoFar() == Calls({"FetchRemote origin"}));
}

TEST_CASE("Network model: pull fails on a diverged branch without merging")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 3, 5)), RemoteBranch("origin", "main")};

	network.Model.Pull();
	network.Update();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Operation == NetworkOperation::Pull);
	CHECK(failure->Error.Message ==
		  "main and origin/main have diverged (3 to push, 5 to pull). Pull can only fast-forward for now; merge or rebase in a "
		  "terminal.");
	CHECK(network.Port.CallsSoFar() == Calls({"FetchRemote origin"}));
	CHECK_FALSE(network.Model.GetState().Running.has_value());
	CHECK_FALSE(network.Git.IsWriteUnsettled());
}

TEST_CASE("Network model: pull fast-forwards through the write queue")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 5)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	model.Pull();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin"}));
	CHECK(writes.Count() == 1);
	CHECK(model.GetState().Running == std::optional<NetworkOperation>{NetworkOperation::Pull});
	CHECK_FALSE(model.GetState().Cancellable);
	CHECK(git.IsUpdatingWorkingTree());

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin", "FastForward refs/remotes/origin/main"}));
	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 5}});
	CHECK_FALSE(model.TakeFailure().has_value());
	CHECK_FALSE(model.GetState().Running.has_value());

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});

	const std::optional<Graphite::GitModel::WriteReport> report = git.GetWriteReport();
	REQUIRE(report.has_value());
	CHECK(report->Progress == Graphite::GitModel::WriteProgress::Done);
	CHECK(report->Subject.Kind == Graphite::GitModel::WriteKind::FastForward);
	CHECK(report->Subject.Commits == std::optional<std::size_t>{5});
}

TEST_CASE("Network model: pull cannot be cancelled once its fast-forward is asked for")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 1)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	model.Pull();
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	model.Cancel(model.GetState().Serial);

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Pull, .Commits = 1}});
}

TEST_CASE("Network model: pull does not merge into a branch checked out while it fetched")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 2)), LocalBranch("feature"), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	model.Pull();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.Branches = {LocalBranch("main", "origin/main", 0, 2), CheckedOut(LocalBranch("feature")), RemoteBranch("origin", "main")};

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const std::optional<NetworkFailureReport> failure = model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "main is no longer checked out, so Pull did not merge origin/main.");
	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin"}));
}

TEST_CASE("Network model: pull's local-changes failure lists the files git named")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 1)), RemoteBranch("origin", "main")};

	const std::string errors = "error: Your local changes to the following files would be overwritten by merge:\n"
							   "\ta.txt\n"
							   "Please commit your changes or stash them before you merge.\n"
							   "Aborting\n";
	network.Port.Outcomes["FastForward"] = Failed("Please commit your changes or stash them before you merge.\nAborting", errors);

	network.Model.Pull();
	network.Update();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Operation == NetworkOperation::Pull);
	CHECK(failure->Error.Message == "Commit or stash your changes to these files first:\na.txt");
	CHECK(failure->Copied == errors);
}

TEST_CASE("Network model: pull without an upstream fails without fetching")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main"))};

	network.Model.Pull();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "main has no upstream branch.");
	CHECK(network.Port.CallsSoFar().empty());
}

TEST_CASE("Network model: push without an upstream goes to the remote given")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "origin"}, Core::Remote{.Name = "fork"}};
	network.Port.Branches = {CheckedOut(LocalBranch("feature")), RemoteBranch("origin", "main")};

	network.Model.Push("fork");
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"PushTo fork (set upstream)"}));
	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push}});
}

TEST_CASE("Network model: push without an upstream asks no remote while there are several")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "origin"}, Core::Remote{.Name = "fork"}};
	network.Port.Branches = {CheckedOut(LocalBranch("feature"))};

	network.Model.Push();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "feature has no upstream branch.");
	CHECK(network.Port.CallsSoFar().empty());
}

TEST_CASE("Network model: push without an upstream goes to the only remote")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "fork"}};
	network.Port.Branches = {CheckedOut(LocalBranch("feature"))};

	network.Model.Push();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"PushTo fork (set upstream)"}));
}

TEST_CASE("Network model: push with an upstream on a remote with a slash in its name sends the branch to its upstream")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "team"}, Core::Remote{.Name = "team/mirror"}};
	network.Port.Branches = {CheckedOut(LocalBranch("main", "team/mirror/release/1.0", 4, 0)),
							 RemoteBranch("team/mirror", "release/1.0")};

	network.Model.Push();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"PushTo team/mirror release/1.0"}));
	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push, .Commits = 4}});
}

TEST_CASE("Network model: detach cancels and never blocks")
{
	ThreadedNetwork network;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	network.Model.Pull();
	REQUIRE(network.Port.WaitUntilHeldEntered());

	const std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
	network.Model.Detach();
	const std::chrono::steady_clock::duration took = std::chrono::steady_clock::now() - before;

	CHECK(took < kDetachAllowance);
	CHECK(network.Port.HeldSawCancel.load());
	CHECK(network.Model.GetState() == NetworkState{});

	network.Model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(network.Model.TakeDone().has_value());
	CHECK_FALSE(network.Model.TakeFailure().has_value());
	CHECK(network.Port.CallsSoFar() == Calls({"FetchAll"}));
}

TEST_CASE("Network model: an answer for the folder before is dropped")
{
	DeferredJobs jobs;
	NetworkPort port;
	Graphite::GitModel git{port, RunJobsInline()};
	NetworkModel model{port, git, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);
	git.SetRoot("C:/repo");

	port.Branches = {RemoteBranch("origin", "main", true)};

	model.Fetch();
	events.Publish(Graphite::WorkspaceOpenedEvent{.Root = "C:/other"});
	model.Fetch();
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
	CHECK_FALSE(model.TakeDone().has_value());
	CHECK_FALSE(model.TakeFailure().has_value());
	CHECK(port.CallsSoFar() == Calls({"FetchAll"}));
	CHECK(model.GetState() == NetworkState{});
}

TEST_CASE("Network model: an answer the folder before had already sent is dropped while the next folder's runs")
{
	DeferredJobs jobs;
	NetworkPort port;
	port.Branches = {RemoteBranch("origin", "main", true)};
	Graphite::GitModel git{port, RunJobsInline()};
	NetworkModel model{port, git, jobs.Submitter()};
	Core::EventBus events;
	model.SubscribeTo(events);
	git.SetRoot("C:/repo");

	model.Fetch();
	jobs.RunAll();
	events.Publish(Graphite::WorkspaceOpenedEvent{.Root = "C:/other"});
	model.Fetch();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK_FALSE(model.TakeDone().has_value());
	CHECK(model.GetState().Running == std::optional<NetworkOperation>{NetworkOperation::Fetch});

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
	CHECK(port.CallsSoFar() == Calls({"FetchAll", "FetchAll"}));
}

TEST_CASE("Network model: the done text is shown for as long as a write's")
{
	InlineNetwork network;

	network.Model.Fetch();
	network.Update();

	REQUIRE(network.Model.GetShownDone().has_value());

	network.Model.OnUpdate(Core::Timestep{Graphite::GitModel::kWriteConfirmationSeconds});

	CHECK_FALSE(network.Model.GetShownDone().has_value());
}

TEST_CASE("Network model: fetching full history runs the unshallow fetch")
{
	InlineNetwork network;

	network.Model.FetchFullHistory();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"FetchFullHistory"}));
	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::FullHistory}});
}

TEST_CASE("Network model: pull does not merge once the port opens another folder")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 2)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	model.Pull();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.WorkingDirectory = "C:/other";

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const std::optional<NetworkFailureReport> failure = model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "The folder changed while Pull ran, so it did not merge origin/main.");
	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin"}));
}

TEST_CASE("Network model: a cancel aimed at an operation that has ended leaves the next one running")
{
	DeferredJobs jobs;
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 1, 0)), RemoteBranch("origin", "main", true)};
	Graphite::GitModel git{port, RunJobsInline()};
	NetworkModel model{port, git, jobs.Submitter()};
	git.SetRoot("C:/repo");

	model.Fetch();
	model.Push();
	const std::uint64_t fetching = model.GetState().Serial;

	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	REQUIRE(model.GetState().Running == std::optional<NetworkOperation>{NetworkOperation::Push});
	CHECK(model.GetState().Serial != fetching);

	model.Cancel(fetching);
	jobs.RunAll();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push, .Commits = 1}});
	CHECK(port.CallsSoFar() == Calls({"FetchAll", "PushTo origin main"}));
}

TEST_CASE("Network model: a push asked while a commit is queued waits for it and pushes the new commit")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 0)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	git.RequestCommit(Core::CommitRequest{.Message = "mine"});
	model.Push();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetState().Running == std::optional<NetworkOperation>{NetworkOperation::Push});
	CHECK(model.GetState().Cancellable);
	CHECK(port.CallsSoFar().empty());

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.PushedTips == std::vector<Core::Oid>{port.CommitTip});
	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push, .Commits = 1}});
}

TEST_CASE("Network model: a pull asked while a write runs starts its fetch once the write is done")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 1)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	git.RequestStage({"a.txt"});
	model.Pull();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CallsSoFar().empty());

	writes.RunOldest();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(port.CallsSoFar() == Calls({"FetchRemote origin"}));
}

TEST_CASE("Network model: a fetch does not wait for the write queue")
{
	NetworkPort port;
	port.Branches = {RemoteBranch("origin", "main", true)};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	git.RequestStage({"a.txt"});
	model.Fetch();

	CHECK(port.CallsSoFar() == Calls({"FetchAll"}));
}

TEST_CASE("Network model: a push waiting for the write queue can be cancelled before git starts")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 1, 0)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	git.RequestStage({"a.txt"});
	model.Push();
	model.Cancel(model.GetState().Serial);
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push, .Cancelled = true}});
	CHECK(model.GetState() == NetworkState{});
	CHECK(port.CallsSoFar().empty());
}

TEST_CASE("Network model: a branch whose upstream is a local branch keeps it and sends HEAD to the only remote")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("feature", "main", 2, 0)), LocalBranch("main")};

	network.Model.Push();
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"PushTo origin feature"}));
	CHECK(network.Model.TakeDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Push}});
}

TEST_CASE("Network model: a branch whose upstream is a local branch keeps it and sends HEAD to the remote given")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "origin"}, Core::Remote{.Name = "fork"}};
	network.Port.Branches = {CheckedOut(LocalBranch("feature", "main", 2, 0)), LocalBranch("main")};

	network.Model.Push("fork");
	network.Update();

	CHECK(network.Port.CallsSoFar() == Calls({"PushTo fork feature"}));
}

TEST_CASE("Network model: a branch whose upstream is a local branch is not pushed while there are several remotes and none given")
{
	InlineNetwork network;
	network.Port.Remotes = {Core::Remote{.Name = "origin"}, Core::Remote{.Name = "fork"}};
	network.Port.Branches = {CheckedOut(LocalBranch("feature", "main", 2, 0)), LocalBranch("main")};

	network.Model.Push();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "feature's upstream is the local branch main, so Push needs a remote chosen.");
	CHECK(network.Port.CallsSoFar().empty());
}

TEST_CASE("Network model: pull that finds the folder changed says so even when the new folder has another branch checked out")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 2)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	model.Pull();
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	port.WorkingDirectory = "C:/other";
	port.Branches = {LocalBranch("main", "origin/main", 0, 2), CheckedOut(LocalBranch("feature")), RemoteBranch("origin", "main")};

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	const std::optional<NetworkFailureReport> failure = model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "The folder changed while Pull ran, so it did not merge origin/main.");
}

TEST_CASE("Network model: detach releases a pull and a push that wait for the write queue")
{
	NetworkPort port;
	port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 1, 1)), RemoteBranch("origin", "main")};

	DeferredJobs writes;
	Graphite::GitModel git{port, RunJobsInline(), writes.Submitter()};
	NetworkModel model{port, git, RunJobsInline()};
	git.SetRoot("C:/repo");

	git.RequestStage({"a.txt"});

	for (const NetworkOperation operation : {NetworkOperation::Pull, NetworkOperation::Push})
	{
		if (operation == NetworkOperation::Pull)
		{
			model.Pull();
		}
		else
		{
			model.Push();
		}

		model.OnUpdate(Core::Timestep{kFrameSeconds});

		REQUIRE(model.GetState().Running == std::optional<NetworkOperation>{operation});

		model.Detach();

		CHECK(model.GetState() == NetworkState{});
	}

	writes.RunAll();
	git.OnUpdate(Core::Timestep{kFrameSeconds});
	model.OnUpdate(Core::Timestep{kFrameSeconds});

	CHECK(model.GetState() == NetworkState{});
	CHECK_FALSE(model.TakeDone().has_value());
	CHECK_FALSE(model.TakeFailure().has_value());
	CHECK(port.CallsSoFar().empty());
}

TEST_CASE("Network model: a cancel of serial zero leaves the running operation alone")
{
	ThreadedNetwork network;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());

	network.Model.Cancel(0);
	std::this_thread::sleep_for(kPollsToSettle);
	network.Port.HeldReleased.store(true);

	CHECK(network.WaitForDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
	CHECK_FALSE(network.Port.HeldSawCancel.load());
}

TEST_CASE("Network model: a serial from before a detach cancels nothing after it")
{
	ThreadedNetwork network;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());
	const std::uint64_t before = network.Model.GetState().Serial;

	network.Model.Detach();
	network.Port.HeldEntered.store(false);
	network.Port.HeldSawCancel.store(false);

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());
	REQUIRE(network.Model.GetState().Serial != before);

	network.Model.Cancel(before);
	std::this_thread::sleep_for(kPollsToSettle);
	network.Port.HeldReleased.store(true);

	CHECK(network.WaitForDone() == std::optional<NetworkDone>{NetworkDone{.Operation = NetworkOperation::Fetch}});
	CHECK_FALSE(network.Port.HeldSawCancel.load());
}

TEST_CASE("Network model: pull refuses when the working directory cannot be read before its fetch")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 1)), RemoteBranch("origin", "main")};
	network.Port.WorkingDirectoryFailure = Core::Error{Core::ErrorCode::NotFound, "the folder is gone"};

	network.Model.Pull();
	network.Update();

	const std::optional<NetworkFailureReport> failure = network.Model.TakeFailure();
	REQUIRE(failure.has_value());
	CHECK(failure->Error.Message == "the folder is gone");
	CHECK(network.Port.CallsSoFar().empty());
}

namespace {

	// FETCH_HEAD's times: a fetch that ended well, one a fetch that then failed or was cancelled wrote, and a later one, as
	// a fetch in a terminal writes.
	constexpr std::chrono::sys_seconds kGoodFetch{std::chrono::seconds{1800000000}};
	constexpr std::chrono::sys_seconds kWrittenByFailure = kGoodFetch + std::chrono::minutes{10};
	constexpr std::chrono::sys_seconds kWrittenLater = kGoodFetch + std::chrono::minutes{20};

	Core::NetworkOutcome Unreachable()
	{
		return Failed("fatal: 'C:/nowhere.git' does not appear to be a git repository",
					  "fatal: 'C:/nowhere.git' does not appear to be a git repository\nfatal: Could not read from remote repository.\n");
	}
}

TEST_CASE("Network model: a failed fetch that wrote FETCH_HEAD keeps the age of the fetch before it shown")
{
	InlineNetwork network;
	network.Port.Network.LastFetch = kGoodFetch;
	network.Port.FetchHeadWrittenAt = kWrittenByFailure;
	network.Port.Outcomes["FetchAll"] = Unreachable();

	CHECK(network.Model.ShownLastFetch(kGoodFetch) == std::optional<std::chrono::sys_seconds>{kGoodFetch});

	network.Model.Fetch();
	network.Update();
	REQUIRE(network.Model.TakeFailure().has_value());

	// The listing reads the time the failed fetch wrote; the bar keeps the good fetch's.
	CHECK(network.Model.ShownLastFetch(kWrittenByFailure) == std::optional<std::chrono::sys_seconds>{kGoodFetch});

	// FETCH_HEAD's time changing again, as a fetch in a terminal changes it, is shown.
	CHECK(network.Model.ShownLastFetch(kWrittenLater) == std::optional<std::chrono::sys_seconds>{kWrittenLater});

	// Another folder knows nothing of this one's fetches.
	network.Model.Detach();
	CHECK(network.Model.ShownLastFetch(kWrittenByFailure) == std::optional<std::chrono::sys_seconds>{kWrittenByFailure});
}

TEST_CASE("Network model: a cancelled fetch that wrote FETCH_HEAD keeps the age of the fetch before it shown")
{
	ThreadedNetwork network;
	network.Port.Network.LastFetch = kGoodFetch;
	network.Port.FetchHeadWrittenAt = kWrittenByFailure;
	network.Port.Held = "FetchAll";

	network.Model.Fetch();
	REQUIRE(network.Port.WaitUntilHeldEntered());
	network.Model.Cancel(network.Model.GetState().Serial);

	const std::optional<NetworkDone> done = network.WaitForDone();
	REQUIRE(done.has_value());
	REQUIRE(done->Cancelled);

	CHECK(network.Model.ShownLastFetch(kWrittenByFailure) == std::optional<std::chrono::sys_seconds>{kGoodFetch});
	CHECK(network.Model.ShownLastFetch(kWrittenLater) == std::optional<std::chrono::sys_seconds>{kWrittenLater});
}

TEST_CASE("Network model: a fetch that ends well shows its own time, and failures in a row keep the last good one's")
{
	InlineNetwork network;
	network.Port.Network.LastFetch = kGoodFetch;
	network.Port.Outcomes["FetchAll"] = Unreachable();

	network.Port.FetchHeadWrittenAt = kWrittenByFailure;
	network.Model.Fetch();
	network.Update();
	REQUIRE(network.Model.TakeFailure().has_value());

	const std::chrono::sys_seconds writtenBySecondFailure = kWrittenByFailure + std::chrono::minutes{1};
	network.Port.FetchHeadWrittenAt = writtenBySecondFailure;
	network.Model.Fetch();
	network.Update();
	REQUIRE(network.Model.TakeFailure().has_value());
	CHECK(network.Model.ShownLastFetch(writtenBySecondFailure) == std::optional<std::chrono::sys_seconds>{kGoodFetch});

	network.Port.Outcomes.erase("FetchAll");
	network.Port.FetchHeadWrittenAt = kWrittenLater;
	network.Model.Fetch();
	network.Update();
	REQUIRE(network.Model.TakeDone().has_value());
	CHECK(network.Model.ShownLastFetch(kWrittenLater) == std::optional<std::chrono::sys_seconds>{kWrittenLater});
}

TEST_CASE("Network model: pull's fetch counts as a fetch, failed or ended well, and a fetch that never wrote FETCH_HEAD changes nothing")
{
	InlineNetwork network;
	network.Port.Branches = {CheckedOut(LocalBranch("main", "origin/main", 0, 0)), RemoteBranch("origin", "main")};
	network.Port.Network.LastFetch = kGoodFetch;
	network.Port.FetchHeadWrittenAt = kWrittenByFailure;
	network.Port.Outcomes["FetchRemote"] = Unreachable();

	network.Model.Pull();
	network.Update();
	REQUIRE(network.Model.TakeFailure().has_value());
	CHECK(network.Model.ShownLastFetch(kWrittenByFailure) == std::optional<std::chrono::sys_seconds>{kGoodFetch});

	// A failure that left FETCH_HEAD as it was keeps what is shown as it was.
	network.Port.FetchHeadWrittenAt.reset();
	network.Model.Pull();
	network.Update();
	REQUIRE(network.Model.TakeFailure().has_value());
	CHECK(network.Model.ShownLastFetch(kWrittenByFailure) == std::optional<std::chrono::sys_seconds>{kGoodFetch});

	network.Port.Outcomes.erase("FetchRemote");
	network.Port.FetchHeadWrittenAt = kWrittenLater;
	network.Model.Pull();

	for (int frame = 0; frame < 4; ++frame)
	{
		network.Update();
	}

	REQUIRE(network.Model.TakeDone().has_value());
	CHECK(network.Model.ShownLastFetch(kWrittenLater) == std::optional<std::chrono::sys_seconds>{kWrittenLater});
}
