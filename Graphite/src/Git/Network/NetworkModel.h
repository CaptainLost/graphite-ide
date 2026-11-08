#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/Command/GitNetwork.h"
#include "Core/Git/Remote.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Git/DeliveryInbox.h"
#include "Git/GitModel.h"
#include "Git/GitPort.h"
#include "Git/Network/NetworkState.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct NetworkDone
	{
		NetworkOperation Operation = NetworkOperation::Fetch;

		// Pull's: the commits the fast-forward brought. Push's: the commits the branch was ahead of its upstream. Empty
		// when not counted: a fetch, a cancel, a push that set the upstream.
		std::optional<std::size_t> Commits;

		bool Cancelled = false;

		friend bool operator==(const NetworkDone&, const NetworkDone&) = default;
	};

	struct NetworkFailureReport
	{
		NetworkOperation Operation = NetworkOperation::Fetch;

		// The toast's message: the hint's sentence first, then git's last lines.
		Core::Error Error;

		// The whole of git's stderr, or the sentence when git never ran.
		std::string Copied;
	};

	// The remote and the branch a failure's sentences name, when they are known.
	struct NetworkNames
	{
		std::optional<std::string> Remote;
		std::optional<std::string> Branch;
	};

	// Runs one fetch, pull or push at a time, on a queue of its own so a long fetch never holds a commit back; the
	// operations asked meanwhile wait their turn in order. Each reads the branches and remotes itself as it starts, so it
	// acts on the repository as it is, not as the last listing showed it; a pull or a push waits first for the writes
	// asked before it, so a commit just made is pushed. Pull fetches its upstream's remote, then hands the fast-forward to
	// the git model's write queue, which reads the branches again just before the merge. UI thread only, but for the jobs
	// it submits.
	// WARNING: Every job captures `this`; the owner declares this after the port and the git model and before the queue
	// behind the submitter.
	class NetworkModel : Core::NonCopyable
	{
	public:
		NetworkModel(GitPort& port, GitModel& git, Core::JobSubmitter submit);

		void Fetch();
		void Pull();

		// Without a remote: the upstream's, or the only remote when the branch has no upstream on one. The branch goes to
		// its upstream only when both have one name; otherwise, and on a remote other than the upstream's, it is published
		// under its own name, which becomes its upstream, unless that upstream is a local branch, which is kept.
		void Push(std::optional<std::string> remote = std::nullopt);

		void FetchFullHistory();

		// Ends the operation of that serial within the runner's poll while it runs, unless it is in Pull's fast-forward; an
		// operation that has ended since its serial was read is left alone, and so is the one that started after it. The
		// waiting ones still run.
		void Cancel(std::uint64_t serial);

		NetworkState GetState() const;

		// Each finished operation, once: a done one or a cancelled one here, a failed one in TakeFailure.
		std::optional<NetworkDone> TakeDone();
		std::optional<NetworkFailureReport> TakeFailure();

		// The last done or cancelled operation, for GitModel::kWriteConfirmationSeconds after it ended, as a write's.
		std::optional<NetworkDone> GetShownDone() const;

		// The time of the last fetch to show, given FETCH_HEAD's as listed: git writes FETCH_HEAD before it reaches the
		// remote, so a fetch of this window's that failed or was cancelled has moved it. While the listed time is the one
		// such a fetch left, the time from before it is shown; any other listed time is a fetch that ended, from here or a
		// terminal, and is shown as it is.
		std::optional<std::chrono::sys_seconds> ShownLastFetch(std::optional<std::chrono::sys_seconds> listed) const;

		void OnUpdate(Core::Timestep timestep);

		// Cancels the running operation, waits for its job to end, which takes at most the runner's poll and the job's own
		// end, and forgets every answer and every operation waiting: before the port is pointed at another folder, and on
		// shutdown.
		// WARNING: Pull's fast-forward, once asked for, still runs on the write queue; only the git model's own folder
		// change drops its answer, which a Detach without one would leave for the next Pull to take.
		void Detach();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Detach()

	private:
		struct Request
		{
			NetworkOperation Operation = NetworkOperation::Fetch;
			std::optional<std::string> Remote;
		};

		// Pull's branch and the upstream its fetch brought up to date, short and full names, and the work tree it fetched
		// for.
		struct PullTarget
		{
			std::string Branch;
			std::string BranchFullName;
			std::string Upstream;
			std::string UpstreamFullName;
			std::filesystem::path WorkingDirectory;
		};

		// FETCH_HEAD's time just before a fetch's git ran and just after it ended.
		struct FetchTimes
		{
			std::optional<std::chrono::sys_seconds> Before;
			std::optional<std::chrono::sys_seconds> After;
		};

		// What a job hands to the UI thread. A refusal before git ran is a failure whose words are the sentence.
		struct JobAnswer
		{
			Core::NetworkOutcome Outcome;
			std::optional<std::size_t> Commits;
			NetworkNames Names;

			// Set when the job ran a fetch: Fetch's, Pull's or the full history's.
			std::optional<FetchTimes> Fetched;

			// Set by Pull's job: the merge to ask once its fetch is done.
			std::optional<PullTarget> FastForward;
		};

		struct RunningOperation
		{
			Request Asked;
			std::uint64_t Serial = 0;

			// Per operation, so a flag set for one can never stop the next.
			Core::Ref<std::atomic<bool>> Cancelled;

			// A pull or a push asked while a write ran or waited: its job starts once the write queue is empty.
			bool AwaitingWrites = false;

			NetworkNames Names;

			// Pull's, once its fast-forward is asked for.
			std::optional<PullTarget> FastForwarding;
		};

		void Ask(Request request);
		void StartNext();
		void Start(const Request& request);
		void SubmitRunning();
		void StartWhenWritesDrained();

		// WARNING: These run on a worker thread. They touch m_Port, m_Progress, m_Percent, m_JobMutex and m_Inbox and nothing
		// else on this object.
		void RunAndDeliver(const Request& request, const std::atomic<bool>& cancelled, std::uint64_t folder);
		JobAnswer RunJob(const Request& request, const std::atomic<bool>& cancelled);
		JobAnswer RunOperation(const Request& request, const std::atomic<bool>& cancelled);
		JobAnswer RunFetch(const std::atomic<bool>& cancelled);
		JobAnswer RunFullHistory(const std::atomic<bool>& cancelled);
		JobAnswer RunPullFetch(const std::atomic<bool>& cancelled);
		JobAnswer RunPush(const std::optional<std::string>& remote, const std::atomic<bool>& cancelled);
		void RecordRemoteDefaults(const std::vector<Core::Remote>& remotes, const std::atomic<bool>& cancelled);

		// Any thread: called with git's percent while it counts.
		void NotePercent(int percent);

		// Runs one of the port's fetches, reading FETCH_HEAD's time around it into the answer.
		void RunFetchOf(JobAnswer& answer, const std::function<Core::NetworkOutcome()>& fetch);

		void Land(std::uint64_t folder, const JobAnswer& answer);
		void NoteFetchEnded(const JobAnswer& answer);
		void LandFastForward(const FastForwardAnswer& answer);
		void Finish(const Core::NetworkOutcome& outcome, std::optional<std::size_t> commits);
		void ShowDone(const NetworkDone& done);
		void CountDownShownDone(Core::Timestep timestep);

		// A job holds m_JobMutex for as long as it runs, so taking it waits for the one running, if any, to end.
		void WaitForRunningJob();

		GitPort& m_Port;
		GitModel& m_Git;
		Core::JobSubmitter m_Submit;
		const Core::ProgressSink m_Progress;

		// WARNING: The inbox's mutex covers the inbox and nothing else; every member after it, up to m_Percent, is touched on
		// the UI thread only.
		DeliveryInbox m_Inbox;

		// Moved by every Detach: an answer of the folder before lands as nothing.
		std::uint64_t m_Folder = 0;

		// WARNING: Never restarts, not even in Detach, so a serial seen before a folder change never names a later operation.
		std::uint64_t m_LastSerial = 0;

		std::optional<RunningOperation> m_Running;
		std::deque<Request> m_Waiting;

		std::deque<NetworkDone> m_Done;
		std::deque<NetworkFailureReport> m_Failures;

		std::optional<NetworkDone> m_Shown;
		float m_ShownSecondsLeft = 0.0f;

		// Set by a fetch of this window's that failed or was cancelled after git wrote FETCH_HEAD: After is the time it left,
		// Before the last good fetch's. Cleared by a fetch that ends well and by Detach.
		std::optional<FetchTimes> m_UnfinishedFetch;

		static constexpr int kNoPercent = -1;

		// Written by the runner's thread, read by the UI thread.
		std::atomic<int> m_Percent{kNoPercent};

		std::mutex m_JobMutex;
	};
}
