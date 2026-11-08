#include "Git/Network/NetworkModel.h"

#include "Core/App/Redraw.h"
#include "Core/Git/Ref.h"
#include "Git/Branches/BranchLookup.h"
#include "Git/Network/NetworkText.h"
#include "Workspace/Workspace.h"

#include <chrono>
#include <exception>
#include <string_view>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kRemoteBranchesRoot = "refs/remotes/";

		Core::NetworkOutcome FailedWith(const std::string& sentence)
		{
			return Core::NetworkOutcome{.Result = Core::NetworkResult::Failed,
										.Failure = {.Shown = sentence, .Copied = sentence, .Hint = Core::NetworkHint::None}};
		}

		// "refs/remotes/<remote>/", where git keeps the remote's branches.
		std::string RemoteBranchesPrefix(const std::string& remote)
		{
			return std::string{kRemoteBranchesRoot} + remote + "/";
		}

		// WARNING: A remote's name may hold a slash, so "refs/remotes/a/b/c" fits both remote "a" and remote "a/b"; the
		// longest name that fits is taken.
		std::optional<std::string> RemoteOfRemoteBranch(const std::string& fullName, const std::vector<Core::Remote>& remotes)
		{
			std::optional<std::string> longest;

			for (const Core::Remote& remote : remotes)
			{
				const bool holdsBranch = fullName.starts_with(RemoteBranchesPrefix(remote.Name));
				const bool longerThanFound = !longest.has_value() || remote.Name.size() > longest->size();

				if (holdsBranch && longerThanFound)
				{
					longest = remote.Name;
				}
			}

			return longest;
		}

		// "main" of "refs/remotes/origin/main".
		std::string NameOnRemote(const std::string& fullName, const std::string& remote)
		{
			return fullName.substr(RemoteBranchesPrefix(remote).size());
		}

		std::optional<std::string> OnlyRemoteOf(const std::vector<Core::Remote>& remotes)
		{
			if (remotes.size() != 1)
			{
				return std::nullopt;
			}

			return remotes.front().Name;
		}

		bool HasRemoteDefault(const std::string& remote, const std::vector<Core::Branch>& branches,
							  const std::vector<Core::Remote>& remotes)
		{
			for (const Core::Branch& branch : branches)
			{
				const bool remoteDefault = branch.Kind == Core::RefKind::Remote && branch.IsRemoteDefault;

				if (remoteDefault && RemoteOfRemoteBranch(branch.FullName, remotes) == remote)
				{
					return true;
				}
			}

			return false;
		}

		// A remote branch, or a local one when the upstream's remote is ".".
		const Core::Branch* UpstreamOf(const Core::Branch& branch, const std::vector<Core::Branch>& branches)
		{
			if (!branch.Upstream.has_value())
			{
				return nullptr;
			}

			if (const Core::Branch* remoteBranch = RemoteBranchNamed(branches, *branch.Upstream); remoteBranch != nullptr)
			{
				return remoteBranch;
			}

			return BranchWithFullName(branches, "refs/heads/" + *branch.Upstream);
		}

		std::optional<std::string> PushRemoteOf(const std::optional<std::string>& asked, const std::optional<std::string>& upstreamRemote,
												const std::vector<Core::Remote>& remotes)
		{
			if (asked.has_value())
			{
				return asked;
			}

			if (upstreamRemote.has_value())
			{
				return upstreamRemote;
			}

			return OnlyRemoteOf(remotes);
		}

		// What Pull and Push read before git runs.
		struct CheckedOutRead
		{
			std::vector<Core::Branch> Branches;
			Core::Branch CheckedOut;
			std::vector<Core::Remote> Remotes;
		};

		// A failure's message is the sentence to say: the read's own, or NoBranchText's when no branch is checked out.
		Core::Result<CheckedOutRead> ReadCheckedOut(GitPort& port)
		{
			Core::Result<std::vector<Core::Branch>> branches = port.QueryBranches();

			if (!branches.HasValue())
			{
				return branches.GetError();
			}

			const Core::Branch* checkedOut = CheckedOutBranch(branches.GetValue());

			if (checkedOut == nullptr)
			{
				return Core::Error{Core::ErrorCode::NotFound, NoBranchText()};
			}

			Core::Branch checkedOutCopy = *checkedOut;
			Core::Result<std::vector<Core::Remote>> remotes = port.QueryRemotes();

			if (!remotes.HasValue())
			{
				return remotes.GetError();
			}

			return CheckedOutRead{.Branches = std::move(branches).GetValue(),
								  .CheckedOut = std::move(checkedOutCopy),
								  .Remotes = std::move(remotes).GetValue()};
		}

		// Why a push without a remote given has none to go to. `localUpstream` is the branch's upstream when it is a local
		// branch, and null otherwise.
		std::string NoPushRemoteText(const Core::Branch& checkedOut, const Core::Branch* localUpstream,
									 const std::vector<Core::Remote>& remotes)
		{
			if (remotes.empty())
			{
				return NoRemoteText();
			}

			if (localUpstream != nullptr)
			{
				return LocalUpstreamText(checkedOut.Name, localUpstream->Name);
			}

			return NoUpstreamText(checkedOut.Name);
		}
	}

	NetworkModel::NetworkModel(GitPort& port, GitModel& git, Core::JobSubmitter submit)
		: m_Port(port), m_Git(git), m_Submit(std::move(submit)), m_Progress([this](int percent) { NotePercent(percent); })
	{
	}

	void NetworkModel::Fetch()
	{
		Ask(Request{.Operation = NetworkOperation::Fetch});
	}

	void NetworkModel::Pull()
	{
		Ask(Request{.Operation = NetworkOperation::Pull});
	}

	void NetworkModel::Push(std::optional<std::string> remote)
	{
		Ask(Request{.Operation = NetworkOperation::Push, .Remote = std::move(remote)});
	}

	void NetworkModel::FetchFullHistory()
	{
		Ask(Request{.Operation = NetworkOperation::FullHistory});
	}

	void NetworkModel::Cancel(std::uint64_t serial)
	{
		const bool aimedAtRunning = m_Running.has_value() && m_Running->Serial == serial;

		if (aimedAtRunning && !m_Running->FastForwarding.has_value())
		{
			m_Running->Cancelled->store(true);
		}
	}

	NetworkState NetworkModel::GetState() const
	{
		if (!m_Running.has_value())
		{
			return NetworkState{};
		}

		NetworkState state{.Running = m_Running->Asked.Operation,
						   .Serial = m_Running->Serial,
						   .Cancellable = !m_Running->FastForwarding.has_value()};

		if (const int percent = m_Percent.load(); percent != kNoPercent)
		{
			state.Percent = percent;
		}

		return state;
	}

	std::optional<NetworkDone> NetworkModel::TakeDone()
	{
		if (m_Done.empty())
		{
			return std::nullopt;
		}

		const NetworkDone taken = m_Done.front();
		m_Done.pop_front();
		return taken;
	}

	std::optional<NetworkFailureReport> NetworkModel::TakeFailure()
	{
		if (m_Failures.empty())
		{
			return std::nullopt;
		}

		NetworkFailureReport taken = std::move(m_Failures.front());
		m_Failures.pop_front();
		return taken;
	}

	std::optional<NetworkDone> NetworkModel::GetShownDone() const
	{
		return m_Shown;
	}

	std::optional<std::chrono::sys_seconds> NetworkModel::ShownLastFetch(std::optional<std::chrono::sys_seconds> listed) const
	{
		if (m_UnfinishedFetch.has_value() && listed == m_UnfinishedFetch->After)
		{
			return m_UnfinishedFetch->Before;
		}

		return listed;
	}

	void NetworkModel::OnUpdate(Core::Timestep timestep)
	{
		// WARNING: Before the deliveries are drained, so a done text shown this frame keeps its full time.
		CountDownShownDone(timestep);

		m_Inbox.Drain();

		const bool fastForwarding = m_Running.has_value() && m_Running->FastForwarding.has_value();

		if (fastForwarding)
		{
			if (const std::optional<FastForwardAnswer> answer = m_Git.TakeFastForward(); answer.has_value())
			{
				LandFastForward(*answer);
			}
		}

		StartWhenWritesDrained();
	}

	void NetworkModel::Detach()
	{
		if (m_Running.has_value())
		{
			m_Running->Cancelled->store(true);
		}

		WaitForRunningJob();

		++m_Folder;

		m_Running.reset();
		m_Waiting.clear();
		m_Done.clear();
		m_Failures.clear();
		m_Shown.reset();
		m_ShownSecondsLeft = 0.0f;
		m_UnfinishedFetch.reset();
		m_Percent.store(kNoPercent);
	}

	void NetworkModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Detach(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Detach(); });
	}

	void NetworkModel::Ask(Request request)
	{
		m_Waiting.push_back(std::move(request));
		StartNext();
	}

	void NetworkModel::StartNext()
	{
		if (m_Running.has_value() || m_Waiting.empty())
		{
			return;
		}

		const Request next = std::move(m_Waiting.front());
		m_Waiting.pop_front();
		Start(next);
	}

	void NetworkModel::Start(const Request& request)
	{
		m_Running = RunningOperation{.Asked = request, .Serial = ++m_LastSerial, .Cancelled = Core::CreateRef<std::atomic<bool>>(false)};
		m_Percent.store(kNoPercent);
		m_Shown.reset();

		// WARNING: A pull or a push reads the branch as the writes asked before it leave it: a commit still on the write
		// queue would be left out of the push it was made for.
		const bool actsOnTheBranch = request.Operation == NetworkOperation::Pull || request.Operation == NetworkOperation::Push;

		if (actsOnTheBranch && m_Git.HasWritesPending())
		{
			m_Running->AwaitingWrites = true;
			return;
		}

		SubmitRunning();
	}

	void NetworkModel::SubmitRunning()
	{
		m_Submit([this, request = m_Running->Asked, cancelled = m_Running->Cancelled, folder = m_Folder]() {
			RunAndDeliver(request, *cancelled, folder);
		});
	}

	void NetworkModel::StartWhenWritesDrained()
	{
		const bool awaitingWrites = m_Running.has_value() && m_Running->AwaitingWrites;

		if (!awaitingWrites)
		{
			return;
		}

		if (m_Running->Cancelled->load())
		{
			const std::optional<std::size_t> notCounted;
			Finish(Core::NetworkOutcome{.Result = Core::NetworkResult::Cancelled}, notCounted);
			return;
		}

		if (m_Git.HasWritesPending())
		{
			return;
		}

		m_Running->AwaitingWrites = false;
		SubmitRunning();
	}

	void NetworkModel::RunAndDeliver(const Request& request, const std::atomic<bool>& cancelled, std::uint64_t folder)
	{
		JobAnswer answer = RunJob(request, cancelled);

		m_Inbox.Post([this, folder, answer = std::move(answer)]() { Land(folder, answer); });
	}

	NetworkModel::JobAnswer NetworkModel::RunJob(const Request& request, const std::atomic<bool>& cancelled)
	{
		const std::lock_guard<std::mutex> runningJob{m_JobMutex};

		// WARNING: A job that starts after a Detach touches nothing, since the port may point at the next folder by then.
		if (cancelled.load())
		{
			return JobAnswer{.Outcome = Core::NetworkOutcome{.Result = Core::NetworkResult::Cancelled}};
		}

		// WARNING: JobQueue abandons a job that throws, so an operation whose job threw would never land and would run for
		// ever.
		try
		{
			return RunOperation(request, cancelled);
		}
		catch (const std::exception& exception)
		{
			return JobAnswer{.Outcome = FailedWith(std::string{"a network operation threw: "} + exception.what())};
		}
		catch (...)
		{
			return JobAnswer{.Outcome = FailedWith("a network operation threw")};
		}
	}

	NetworkModel::JobAnswer NetworkModel::RunOperation(const Request& request, const std::atomic<bool>& cancelled)
	{
		// WARNING: No default, so an operation added later is a warning here, and warnings fail the build.
		switch (request.Operation)
		{
		case NetworkOperation::Fetch:
			return RunFetch(cancelled);
		case NetworkOperation::Pull:
			return RunPullFetch(cancelled);
		case NetworkOperation::Push:
			return RunPush(request.Remote, cancelled);
		case NetworkOperation::FullHistory:
			return RunFullHistory(cancelled);
		}

		return JobAnswer{};
	}

	NetworkModel::JobAnswer NetworkModel::RunFetch(const std::atomic<bool>& cancelled)
	{
		// WARNING: A fetch whose remotes cannot be read runs all the same; only the sign-in sentence's name and the
		// default branches go without them.
		const Core::Result<std::vector<Core::Remote>> listed = m_Port.QueryRemotes();
		const std::vector<Core::Remote> remotes = listed.HasValue() ? listed.GetValue() : std::vector<Core::Remote>{};

		JobAnswer answer;
		answer.Names.Remote = OnlyRemoteOf(remotes);
		RunFetchOf(answer, [&] { return m_Port.FetchAll(cancelled, m_Progress); });

		if (answer.Outcome.Result == Core::NetworkResult::Done)
		{
			RecordRemoteDefaults(remotes, cancelled);
		}

		return answer;
	}

	NetworkModel::JobAnswer NetworkModel::RunFullHistory(const std::atomic<bool>& cancelled)
	{
		const Core::Result<std::vector<Core::Remote>> listed = m_Port.QueryRemotes();

		JobAnswer answer;
		answer.Names.Remote = listed.HasValue() ? OnlyRemoteOf(listed.GetValue()) : std::nullopt;
		RunFetchOf(answer, [&] { return m_Port.FetchFullHistory(cancelled, m_Progress); });

		return answer;
	}

	void NetworkModel::RunFetchOf(JobAnswer& answer, const std::function<Core::NetworkOutcome()>& fetch)
	{
		const std::optional<std::chrono::sys_seconds> before = m_Port.QueryLastFetch();
		answer.Outcome = fetch();
		answer.Fetched = FetchTimes{.Before = before, .After = m_Port.QueryLastFetch()};
	}

	void NetworkModel::RecordRemoteDefaults(const std::vector<Core::Remote>& remotes, const std::atomic<bool>& cancelled)
	{
		if (remotes.empty() || cancelled.load())
		{
			return;
		}

		const Core::Result<std::vector<Core::Branch>> branches = m_Port.QueryBranches();

		if (!branches.HasValue())
		{
			return;
		}

		const Core::ProgressSink noProgress;

		for (const Core::Remote& remote : remotes)
		{
			if (cancelled.load())
			{
				return;
			}

			if (HasRemoteDefault(remote.Name, branches.GetValue(), remotes))
			{
				continue;
			}

			// A remote whose server names no default, as an empty one, keeps none, as before the fetch.
			static_cast<void>(m_Port.SetRemoteHeadAuto(cancelled, noProgress, remote.Name));
		}
	}

	NetworkModel::JobAnswer NetworkModel::RunPullFetch(const std::atomic<bool>& cancelled)
	{
		JobAnswer answer;
		const Core::Result<CheckedOutRead> read = ReadCheckedOut(m_Port);

		if (!read.HasValue())
		{
			answer.Outcome = FailedWith(read.GetError().Message);
			return answer;
		}

		const Core::Branch& checkedOut = read.GetValue().CheckedOut;
		answer.Names.Branch = checkedOut.Name;
		const Core::Branch* upstream = UpstreamOf(checkedOut, read.GetValue().Branches);

		if (upstream == nullptr)
		{
			answer.Outcome = FailedWith(checkedOut.MissingUpstream.has_value() ? GoneUpstreamText(checkedOut.Name, *checkedOut.MissingUpstream)
																			   : NoUpstreamText(checkedOut.Name));
			return answer;
		}

		const Core::Result<std::filesystem::path> workingDirectory = m_Port.GetWorkingDirectory();

		if (!workingDirectory.HasValue())
		{
			answer.Outcome = FailedWith(workingDirectory.GetError().Message);
			return answer;
		}

		answer.Names.Remote = RemoteOfRemoteBranch(upstream->FullName, read.GetValue().Remotes);
		answer.FastForward = PullTarget{.Branch = checkedOut.Name,
										.BranchFullName = checkedOut.FullName,
										.Upstream = upstream->Name,
										.UpstreamFullName = upstream->FullName,
										.WorkingDirectory = workingDirectory.GetValue()};

		// An upstream that is a local branch is up to date already, with nothing to fetch.
		if (answer.Names.Remote.has_value())
		{
			RunFetchOf(answer, [&] { return m_Port.FetchRemote(cancelled, m_Progress, *answer.Names.Remote); });
		}

		return answer;
	}

	NetworkModel::JobAnswer NetworkModel::RunPush(const std::optional<std::string>& remote, const std::atomic<bool>& cancelled)
	{
		JobAnswer answer;
		const Core::Result<CheckedOutRead> read = ReadCheckedOut(m_Port);

		if (!read.HasValue())
		{
			answer.Outcome = FailedWith(read.GetError().Message);
			return answer;
		}

		const Core::Branch& checkedOut = read.GetValue().CheckedOut;
		const std::vector<Core::Remote>& remotes = read.GetValue().Remotes;
		answer.Names.Branch = checkedOut.Name;

		const Core::Branch* upstream = UpstreamOf(checkedOut, read.GetValue().Branches);
		const bool upstreamIsLocal = upstream != nullptr && upstream->Kind == Core::RefKind::Local;
		const std::optional<std::string> upstreamRemote =
			upstream != nullptr ? RemoteOfRemoteBranch(upstream->FullName, remotes) : std::nullopt;
		const std::optional<std::string> target = PushRemoteOf(remote, upstreamRemote, remotes);

		if (!target.has_value())
		{
			answer.Outcome = FailedWith(NoPushRemoteText(checkedOut, upstreamIsLocal ? upstream : nullptr, remotes));
			return answer;
		}

		answer.Names.Remote = target;

		// WARNING: --set-upstream would replace the local branch the user made this one follow.
		if (upstreamIsLocal)
		{
			answer.Outcome = m_Port.PushTo(cancelled, m_Progress, *target, checkedOut.Name);
			return answer;
		}

		// WARNING: Only a branch of its upstream's own name, compared exactly, goes to the upstream, as git's own default
		// push has it. A branch made from origin/main tracks origin/main, and HEAD:main would move the server's main to the
		// branch's commits; any other branch is published under its own name and tracks that from then on.
		const bool toUpstream = upstreamRemote == target && checkedOut.Name == NameOnRemote(upstream->FullName, *target);

		if (!toUpstream)
		{
			const std::optional<std::string> setsUpstream;
			answer.Outcome = m_Port.PushTo(cancelled, m_Progress, *target, setsUpstream);
			return answer;
		}

		if (checkedOut.DistanceKnown)
		{
			answer.Commits = checkedOut.Ahead;
		}

		answer.Outcome = m_Port.PushTo(cancelled, m_Progress, *target, NameOnRemote(upstream->FullName, *target));
		return answer;
	}

	void NetworkModel::NotePercent(int percent)
	{
		if (m_Percent.exchange(percent) != percent)
		{
			Core::Redraw::Request();
		}
	}

	void NetworkModel::Land(std::uint64_t folder, const JobAnswer& answer)
	{
		if (folder != m_Folder || !m_Running.has_value())
		{
			return;
		}

		NoteFetchEnded(answer);
		m_Running->Names = answer.Names;

		const bool fetchedForPull = answer.FastForward.has_value() && answer.Outcome.Result == Core::NetworkResult::Done;

		if (!fetchedForPull)
		{
			Finish(answer.Outcome, answer.Commits);
			return;
		}

		// A cancel that came once git's fetch had ended still stops Pull before its merge, which nothing stops once asked.
		if (m_Running->Cancelled->load())
		{
			const std::optional<std::size_t> notCounted;
			Finish(Core::NetworkOutcome{.Result = Core::NetworkResult::Cancelled}, notCounted);
			return;
		}

		m_Running->FastForwarding = answer.FastForward;
		m_Percent.store(kNoPercent);
		m_Git.RequestFastForward(FastForwardRequest{.Branch = answer.FastForward->BranchFullName,
													.Upstream = answer.FastForward->UpstreamFullName,
													.WorkingDirectory = answer.FastForward->WorkingDirectory});
	}

	void NetworkModel::NoteFetchEnded(const JobAnswer& answer)
	{
		if (!answer.Fetched.has_value())
		{
			return;
		}

		if (answer.Outcome.Result == Core::NetworkResult::Done)
		{
			m_UnfinishedFetch.reset();
			return;
		}

		const FetchTimes& times = *answer.Fetched;

		if (times.Before == times.After)
		{
			return;
		}

		// After failures in a row, the time before the first of them is the last good fetch's.
		const bool followsAnother = m_UnfinishedFetch.has_value() && m_UnfinishedFetch->After == times.Before;
		m_UnfinishedFetch = FetchTimes{.Before = followsAnother ? m_UnfinishedFetch->Before : times.Before, .After = times.After};
	}

	void NetworkModel::LandFastForward(const FastForwardAnswer& answer)
	{
		const PullTarget target = *m_Running->FastForwarding;
		const std::optional<std::size_t> notCounted;

		if (answer.Failure.has_value())
		{
			Finish(FailedWith(answer.Failure->Message), notCounted);
			return;
		}

		if (answer.FolderChanged)
		{
			Finish(FailedWith(FolderChangedText(target.Upstream)), notCounted);
			return;
		}

		if (!answer.CheckedOut)
		{
			Finish(FailedWith(NoLongerCheckedOutText(target.Branch, target.Upstream)), notCounted);
			return;
		}

		const bool diverged = !answer.Merge.has_value();

		if (diverged)
		{
			Finish(FailedWith(DivergedText(target.Branch, target.Upstream, answer.Ahead, answer.Behind)), notCounted);
			return;
		}

		const std::optional<std::size_t> brought = answer.DistanceKnown ? std::optional<std::size_t>{answer.Behind} : std::nullopt;
		Finish(*answer.Merge, brought);
	}

	void NetworkModel::Finish(const Core::NetworkOutcome& outcome, std::optional<std::size_t> commits)
	{
		const NetworkOperation operation = m_Running->Asked.Operation;
		const NetworkNames names = m_Running->Names;

		m_Running.reset();
		m_Percent.store(kNoPercent);

		// WARNING: No default, so a result added later is a warning here, and warnings fail the build.
		switch (outcome.Result)
		{
		case Core::NetworkResult::Done:
			ShowDone(NetworkDone{.Operation = operation, .Commits = commits});
			break;
		case Core::NetworkResult::Cancelled:
			ShowDone(NetworkDone{.Operation = operation, .Cancelled = true});
			break;
		case Core::NetworkResult::Failed:
			m_Failures.push_back(NetworkFailureReport{
				.Operation = operation,
				.Error = Core::Error{Core::ErrorCode::IoFailure, FailureMessage(operation, outcome.Failure, names)},
				.Copied = outcome.Failure.Copied});
			break;
		}

		StartNext();
	}

	void NetworkModel::ShowDone(const NetworkDone& done)
	{
		m_Done.push_back(done);
		m_Shown = done;
		m_ShownSecondsLeft = GitModel::kWriteConfirmationSeconds;
		Core::Redraw::RequestIn(std::chrono::duration<double>(m_ShownSecondsLeft));
	}

	void NetworkModel::CountDownShownDone(Core::Timestep timestep)
	{
		if (!m_Shown.has_value())
		{
			return;
		}

		m_ShownSecondsLeft -= timestep.GetSeconds();

		if (m_ShownSecondsLeft <= 0.0f)
		{
			m_Shown.reset();
			return;
		}

		Core::Redraw::RequestIn(std::chrono::duration<double>(m_ShownSecondsLeft));
	}

	void NetworkModel::WaitForRunningJob()
	{
		const std::lock_guard<std::mutex> jobEnded{m_JobMutex};
	}
}
