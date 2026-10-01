#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Git/FileTexts.h"
#include "Git/GitPort.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// How a query reaches a worker. Injected rather than taken as a Core::JobQueue so the
	// tests can run a job where they submitted it and stay free of threads.
	using JobSubmitter = std::function<void(std::function<void()>)>;

	// What a discard is asked to do, as two lists rather than one. They are two different
	// outcomes — restored from the index, and deleted from disk — and merging them would
	// make the effect depend on something the caller did not check.
	struct DiscardRequest
	{
		std::vector<std::filesystem::path> Restore;
		std::vector<std::filesystem::path> Delete;
	};

	// Holds what the git views draw and decides when it is stale.
	//
	// Two rules run through the whole class. Results are immutable and handed out by
	// shared pointer, so a view that took one keeps drawing a consistent list even as the
	// next refresh lands. And every request carries a generation, so a slow query that
	// finishes after a newer one is dropped instead of putting old data back on screen.
	// Every job submitted below captures `this`, so the model requires that the queue
	// behind its submitter is destroyed - and its workers joined - before the model
	// itself is. An owner holding both the model and the queue must declare the model
	// BEFORE the queue, so reverse member destruction tears the queue down first, before
	// this object goes away. Getting the order backwards is a use-after-free that will
	// not show up under the inline submitter the tests use.
	class GitModel : Core::NonCopyable
	{
	public:
		GitModel(GitPort& port, JobSubmitter submit);

		// Applies whatever the workers finished since the last frame. Called once per
		// frame from the layer; results only become visible here, never mid-frame.
		void OnUpdate(Core::Timestep ts);

		void RequestStatus();

		// Every untracked file under one directory, fetched when a reader opens that
		// directory up. Kept per directory rather than in one list, because each is a
		// separate question with a separate answer, and dropped whole on every refresh:
		// unlike a commit's diff, what is untracked under a directory changes as the
		// working tree does.
		//
		// Asking for a directory already held, already in flight, or already failed does
		// nothing. A panel asks on every frame it draws an expanded folder, and a
		// permanent failure must not turn that into a worker job per frame.
		void RequestUntrackedIn(std::filesystem::path directory);

		// Null while the answer is in flight, after a failure, and for a directory never
		// asked about. The caller's answer is the same in every case: draw no children
		// under that folder yet.
		std::shared_ptr<const Core::WorkingTreeStatus> GetUntrackedIn(
			const std::filesystem::path& directory) const;

		void RequestBranches();

		// Refreshed alongside the status - see OnUpdate - so the bar's branch and its change
		// count never describe two different moments for longer than one query.
		void RequestHead();
		void RequestLog(Core::LogRequest request);

		// The working tree against HEAD: a diff that changes on every keystroke, so there is
		// nothing to hold past the next refresh. One slot, replaced whenever a fresher answer
		// lands, the same way m_Status and m_Branches are - unlike a commit's diff, which
		// never changes and is therefore kept in the keyed cache below (RequestDiffFor).
		void RequestWorkingTreeDiff(Core::DiffRequest request);

		// The diff of one commit against its first parent. Requested by expanding a row,
		// and held afterwards: a commit's diff cannot change, so re-asking would be pure
		// waste — and the list asks about every expanded commit on every frame it draws.
		// For the working tree's own diff, which does change and is therefore not cached
		// this way, see RequestWorkingTreeDiff.
		//
		// Asking for a commit already held, already in flight, or already failed, does
		// nothing — a permanent failure must not turn the list's every-frame ask into a
		// worker job every frame. ForgetFailedDiffFor is what gives a failed entry a way back.
		void RequestDiffFor(const Core::Oid& commit);

		// Drops a cached entry, but only if it is marked failed - a successful diff stays,
		// which is what makes this a cache of recently-seen commits rather than a holder for
		// only the currently-expanded ones. Called when a row collapses: for a failed entry
		// it is the reader's recourse, obvious without being told, since expanding the row
		// again is the same gesture that asked for the diff in the first place. Clears
		// m_CommitDiffError together with the entry, so the error line does not outlive the
		// row it described.
		void ForgetFailedDiffFor(const Core::Oid& commit);

		// Whether the last answer for this commit's diff was a failure, still held. True until
		// ForgetFailedDiffFor drops it; false while the answer is in flight, once it has arrived
		// whole, and for a commit never asked about or since evicted.
		bool IsDiffFailed(const Core::Oid& commit) const;

		// Null while the answer is in flight, after a failure, and for a commit that was
		// evicted. In every case the caller's response is the same: draw nothing under
		// that row yet.
		std::shared_ptr<const Core::Diff> GetDiffFor(const Core::Oid& commit) const;

		// Thirty-two is well past any number of rows a reader expands at once, and bounds
		// the memory a long scroll can accumulate. Evicting the least recently asked for
		// costs a re-query if the reader scrolls back, which is a worker-thread walk they
		// will not notice, against holding every diff they ever passed.
		static constexpr std::size_t kCachedDiffs = 32;

		// Both sides of one file's text around a commit, for the diff view to hand to the
		// lexer. Keyed by commit and `key` together - not by commit alone, as RequestDiffFor
		// is - because one commit touches many files and each needs its own entry. Mirrors
		// RequestDiffFor in every other respect: held once fetched, since a commit's text
		// cannot change either, and a failed entry is suppressed until ForgetFailedTextsFor
		// gives it a way back.
		//
		// `key` is the path that names the file, which the caller works out with ShownPath
		// and which is never empty. `oldPath` and `newPath` are the blob paths to read, and
		// either may be empty - that is how a side is said to be absent - which is exactly
		// why neither can serve as the key: every file deleted in one commit has the same
		// empty new path, and every file added has the same empty old one.
		void RequestTextsFor(
			const Core::Oid& commit,
			const std::filesystem::path& key,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath);

		// Drops a cached entry, but only if it is marked failed - mirrors ForgetFailedDiffFor
		// exactly, including clearing m_TextsError together with the entry. A transient read
		// failure must not poison a file forever: without this, Failed would suppress every
		// re-request until the LRU happens to evict the entry, which for a small,
		// frequently-revisited set of files may be never.
		void ForgetFailedTextsFor(const Core::Oid& commit, const std::filesystem::path& key);

		// Null while the answer is in flight, after a failure, and for a file that was
		// evicted. As with GetDiffFor, the caller's response is the same in every case: draw
		// nothing yet. Keyed the same way RequestTextsFor is - see its comment on `key`.
		std::shared_ptr<const FileTexts> GetTextsFor(
			const Core::Oid& commit, const std::filesystem::path& key) const;

		// Same bound and the same reasoning as kCachedDiffs, kept as its own constant
		// because the two caches are keyed differently and hold different-sized entries -
		// tying their bounds together would be a coincidence, not a decision.
		static constexpr std::size_t kCachedTexts = 32;

		// The directory status paths are relative to, which is not always GetRoot(): opening a
		// subdirectory of a repository still resolves upward to the repository's own working
		// directory (see GitPort::GetWorkingDirectory), and a caller joining a status path onto
		// GetRoot() instead would build a path that does not exist. Fetched alongside the
		// status query and delivered with it, so this is always in step with whatever
		// GetStatus() currently returns - empty until the first status has landed.
		const std::filesystem::path& GetWorkingDirectory() const { return m_WorkingDirectory; }

		// Null until the first result for that query has been applied.
		std::shared_ptr<const Core::WorkingTreeStatus> GetStatus() const { return m_Status; }
		std::shared_ptr<const std::vector<Core::Branch>> GetBranches() const { return m_Branches; }
		std::shared_ptr<const Core::HeadState> GetHead() const { return m_Head; }
		std::shared_ptr<const Core::LogPage> GetLog() const { return m_Log; }
		std::shared_ptr<const Core::Diff> GetWorkingTreeDiff() const { return m_WorkingTreeDiff; }

		// The whole file's text on both sides of the working-tree diff above, fetched with it
		// and replaced with it. Null while none has arrived, and null for a file that is binary
		// or too large to lex - in every case the caller's answer is the same: draw the diff
		// without the colouring and the whole-file view that this text feeds.
		std::shared_ptr<const FileTexts> GetWorkingTreeTexts() const { return m_WorkingTreeTexts; }

		// The failure a panel should show, if any. Each query kind keeps its own error, set
		// by its own failure and cleared only by its own success, so a branches refresh
		// that succeeds can never hide a status refresh that is still failing. When more
		// than one is set this returns the first in the order status, branches, log,
		// working-tree diff, commit diff, file texts - status is checked most often and is
		// the one a user most needs to see fail, and a file-texts failure is the least
		// likely of the six to be what a user needs told first.
		// Each query's own failure, for a reader that has to tell one kind of failure from
		// another rather than show whichever came first (see HistoryPlaceholder).
		const std::optional<Core::Error>& GetStatusError() const { return m_StatusError; }
		const std::optional<Core::Error>& GetLogError() const { return m_LogError; }

		// Not folded into GetLastError: the head is read only by the status bar, which draws
		// nothing rather than an error when it has no head, and the panels that show
		// GetLastError have their own status error to explain the same failure.
		const std::optional<Core::Error>& GetHeadError() const { return m_HeadError; }

		const std::optional<Core::Error>& GetLastError() const
		{
			if (m_StatusError.has_value())
			{
				return m_StatusError;
			}

			if (m_BranchesError.has_value())
			{
				return m_BranchesError;
			}

			if (m_LogError.has_value())
			{
				return m_LogError;
			}

			if (m_WorkingTreeDiffError.has_value())
			{
				return m_WorkingTreeDiffError;
			}

			if (m_CommitDiffError.has_value())
			{
				return m_CommitDiffError;
			}

			return m_TextsError;
		}

		// The writes. Each one queues; see the write queue's comment below for why they are
		// never sent concurrently.
		void RequestStage(std::vector<std::filesystem::path> paths);
		void RequestUnstage(std::vector<std::filesystem::path> paths);
		void RequestDiscard(DiscardRequest request);
		void RequestCommit(Core::CommitRequest request);

		// git init at the open folder. Queued like the other writes, so a panel greys its
		// button through IsWriteInFlight and reads a failure through GetWriteError.
		void RequestInit();

		// For a panel that greys its buttons out. False does not mean nothing is pending —
		// it means nothing is happening at this instant.
		bool IsWriteInFlight() const { return m_WriteInFlight; }

		// True from a successful init until the first status asked for after it is applied.
		// Between the two the write is done but the status still says the folder is not a
		// repository, so a panel reading only IsWriteInFlight would offer the button again.
		bool IsAwaitingStatusAfterInit() const
		{
			return m_InitSettlesAfter.has_value() && m_StatusApplied <= *m_InitSettlesAfter;
		}

		// Kept apart from GetLastError(): a write is the direct answer to a click, so it
		// belongs to the panel that issued it rather than to the list of refresh failures,
		// and a refresh succeeding must never clear it.
		const std::optional<Core::Error>& GetWriteError() const { return m_WriteError; }
		void DismissWriteError() { m_WriteError.reset(); }

		// One-shot: answers once after a successful commit and forgets. This is how a panel
		// learns to clear its message box, rather than inferring it from the status having
		// changed — which would also fire for a commit made in a terminal.
		std::optional<Core::Oid> TakeCommitted();

		// One-shot, mirroring TakeCommitted: every path the last successful discard touched,
		// restored and deleted together. The caller uses it to tell the editor that a change
		// it is about to see came from here.
		std::vector<std::filesystem::path> TakeDiscarded();

		// How many writes - stage, unstage, discard, delete, commit - this model has applied
		// successfully. Only ever grows, so any number of readers can each compare it against
		// the value they last saw, where a one-shot Take would let the first reader starve the
		// rest. This is how something outside the model learns that HEAD or the index moved
		// under it without waiting for the file watcher, which does not see `.git/` when the
		// workspace is a subfolder of the repository or a linked worktree.
		std::uint64_t GetAppliedWriteCount() const { return m_AppliedWrites; }

		// Without a root the model answers nothing: a file-change event that arrives
		// before a folder is open has no repository to ask about.
		void SetRoot(std::filesystem::path root);

		void Clear();

		void SubscribeTo(Core::EventBus& events);

		// How long a burst of file-change events is allowed to keep coalescing. A save
		// touches several files, and refreshing per file would run the same query five
		// times for one action.
		static constexpr float kDebounceSeconds = 0.15f;

		// A ceiling on how long coalescing can postpone the refresh, measured from the first
		// event of the burst rather than the most recent one. Without this, a sustained
		// stream of change events - a build writing into an output directory, which
		// Workspace::Update republishes per settled batch - would keep resetting the quiet
		// timer and starve the refresh for as long as the stream continues, rather than
		// coalescing to one refresh per quiet period.
		static constexpr float kMaxDebounceSeconds = 1.0f;

	private:
		using Delivery = std::function<void()>;

		void Deliver(Delivery delivery);

		void MarkDirty();

		// Asks the frame loop for the frame the debounce runs out on. A mark can come from a
		// result that woke the loop for a few frames only, and the loop would otherwise sleep
		// through the debounce until its idle tick.
		void RequestDebounceFrame() const;

		struct CachedDiff
		{
			// Generations from the model-wide m_CommitDiffGeneration counter, never from a
			// counter local to the entry: Clear() erases the entry itself, and an entry
			// recreated after a clear starts its Applied back at zero. If Requested also
			// restarted at a small number local to the entry, a delivery from before the
			// clear could out-rank a delivery from after it. Reusing the shared counter
			// means every generation ever handed out is unique for the model's lifetime, so
			// whichever delivery actually carries the larger generation is the newer one,
			// clear or no clear.
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			std::shared_ptr<const Core::Diff> Diff;

			// Set when the query answered with an error rather than a diff. Checked by
			// RequestDiffFor so a permanent failure does not turn the list's every-frame ask
			// into a worker job every frame — only ForgetFailedDiffFor clears it.
			bool Failed = false;

			// Counts up on every request and every read, so eviction can pick the entry
			// nobody has looked at for longest. A counter rather than a clock: the model
			// must stay testable without one.
			std::uint64_t LastTouched = 0;
		};

		void Touch(CachedDiff& entry) const;
		void EvictIfFull();

		// One commit touches many files, so the texts cache is keyed by commit and the path
		// that names the file together - the diff cache above needs only the commit, since it
		// holds one entry per commit rather than one per file. See RequestTextsFor for why
		// that path is not the new path.
		struct TextsKey
		{
			Core::Oid Commit;
			std::filesystem::path Path;

			friend bool operator==(const TextsKey&, const TextsKey&) = default;
		};

		struct TextsKeyHash
		{
			std::size_t operator()(const TextsKey& key) const noexcept
			{
				return std::hash<Core::Oid>{}(key.Commit) ^
					(std::hash<std::filesystem::path>{}(key.Path) << 1);
			}
		};

		struct CachedTexts
		{
			// From the model-wide m_TextsGeneration counter, for the same reason
			// CachedDiff::Requested is - see that comment.
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			std::shared_ptr<const FileTexts> Texts;

			// Suppresses a re-request the same way CachedDiff::Failed does.
			// ForgetFailedTextsFor is what gives a failed entry a way back.
			bool Failed = false;

			// Same purpose as CachedDiff::LastTouched: lets eviction pick the entry nobody
			// has looked at for longest.
			std::uint64_t LastTouched = 0;
		};

		void Touch(CachedTexts& entry) const;
		void EvictTextsIfFull();

		struct CachedUntracked
		{
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			std::shared_ptr<const Core::WorkingTreeStatus> Listing;

			bool Failed = false;
		};

		// Keyed by the path's UTF-8 string, because std::filesystem::path has no standard
		// hash. No LRU bound here, unlike kCachedDiffs: this map holds one entry per
		// directory the reader expanded since the last refresh, and every refresh empties
		// it, so there is nothing to accumulate.
		std::unordered_map<std::string, CachedUntracked> m_Untracked;

		// Model-wide and never restarts, for the reason spelled out on CachedDiff::Requested:
		// an entry recreated after a Clear() would otherwise let a pre-clear delivery
		// outrank a post-clear one.
		std::uint64_t m_UntrackedGeneration = 0;

		enum class WriteKind
		{
			Stage,
			Unstage,
			Discard,
			Commit,
			Init
		};

		// One queued write. Carries every kind's payload rather than a variant: there are
		// five kinds and two payloads, and a variant here would cost more to read than the
		// two fields it would save.
		struct WriteJob
		{
			WriteKind Kind = WriteKind::Stage;
			std::vector<std::filesystem::path> Paths;
			DiscardRequest Discard;
			Core::CommitRequest Commit;

			// The generation this was queued under. Unlike a query's generation this never
			// orders two results — there is no such thing as a stale commit — it only says
			// whether the folder it was meant for is still open.
			std::uint64_t Generation = 0;
		};

		// What a write came back with. An error or, for a commit, an id; never both.
		struct WriteOutcome
		{
			std::optional<Core::Error> Failure;
			std::optional<Core::Oid> Committed;
		};

		void Enqueue(WriteJob job);

		// Starts the next queued write if none is running. Called after every enqueue and
		// after every completion, which is what keeps the queue moving without a timer.
		void PumpWrites();

		// Runs on a worker thread. Touches m_Port and nothing else on this object.
		WriteOutcome RunWrite(const WriteJob& job);

		// Runs on the UI thread, out of the inbox, like every other delivery.
		void ApplyWrite(const WriteJob& job, WriteOutcome outcome);

		std::filesystem::path m_Root;

		bool m_Dirty = false;
		float m_DirtyFor = 0.0f;

		// Time since the burst's first mark, not its most recent one - see kMaxDebounceSeconds.
		float m_DirtySince = 0.0f;

		GitPort& m_Port;
		JobSubmitter m_Submit;

		// Written by workers, drained by OnUpdate. The mutex covers this and nothing else:
		// every snapshot below is only ever touched on the UI thread — the write queue's
		// state included, since PumpWrites, ApplyWrite and Enqueue all run there too.
		std::mutex m_InboxMutex;
		std::vector<Delivery> m_Inbox;

		// Queued rather than concurrent, because a user clicking stage on three rows must
		// get three writes rather than one and two dropped clicks — and because two writes
		// at once would contend for the same index lock for no gain.
		std::deque<WriteJob> m_WriteQueue;
		bool m_WriteInFlight = false;

		// Bumped by Clear(), so a write in flight when the folder closes is recognised as
		// belonging to the folder that is gone.
		std::uint64_t m_WriteGeneration = 0;

		std::optional<Core::Error> m_WriteError;
		std::optional<Core::Oid> m_Committed;
		std::vector<std::filesystem::path> m_Discarded;
		std::uint64_t m_AppliedWrites = 0;

		// The last status generation requested when an init was applied. A status asked for
		// before then - even one landing afterwards - still describes the folder without a
		// repository, so only one numbered past this settles it.
		std::optional<std::uint64_t> m_InitSettlesAfter;

		std::uint64_t m_StatusRequested = 0;
		std::uint64_t m_StatusApplied = 0;
		std::uint64_t m_BranchesRequested = 0;
		std::uint64_t m_BranchesApplied = 0;
		std::uint64_t m_HeadRequested = 0;
		std::uint64_t m_HeadApplied = 0;
		std::uint64_t m_LogRequested = 0;
		std::uint64_t m_LogApplied = 0;
		std::uint64_t m_WorkingTreeDiffRequested = 0;
		std::uint64_t m_WorkingTreeDiffApplied = 0;

		// See GetWorkingDirectory(): fetched and applied alongside m_Status, never lazily.
		std::filesystem::path m_WorkingDirectory;

		std::shared_ptr<const Core::WorkingTreeStatus> m_Status;
		std::shared_ptr<const std::vector<Core::Branch>> m_Branches;
		std::shared_ptr<const Core::HeadState> m_Head;
		std::shared_ptr<const Core::LogPage> m_Log;

		// The last question asked about the history, so a refresh can ask it again. Unset
		// until something asks for a log, and dropped by Clear() with the rest of the
		// folder's state - see RequestLog and OnUpdate for both halves of the reasoning.
		std::optional<Core::LogRequest> m_LastLogRequest;
		std::shared_ptr<const Core::Diff> m_WorkingTreeDiff;
		std::shared_ptr<const FileTexts> m_WorkingTreeTexts;

		// Mutable because GetDiffFor is const and still counts as a use — an entry the
		// reader is looking at every frame must not be the one evicted.
		mutable std::unordered_map<Core::Oid, CachedDiff> m_CommitDiffs;
		mutable std::uint64_t m_DiffClock = 0;

		// Shared by every commit-diff request, never reset by Clear() - see the comment on
		// CachedDiff::Requested for why a counter local to the entry cannot do this job.
		std::uint64_t m_CommitDiffGeneration = 0;

		// Mirrors m_CommitDiffs in every respect described on CachedTexts above.
		mutable std::unordered_map<TextsKey, CachedTexts, TextsKeyHash> m_Texts;
		mutable std::uint64_t m_TextsClock = 0;

		// Shared by every texts request, never reset by Clear() - see the comment on
		// CachedDiff::Requested for why a counter local to the entry cannot do this job.
		std::uint64_t m_TextsGeneration = 0;

		std::optional<Core::Error> m_StatusError;
		std::optional<Core::Error> m_BranchesError;
		std::optional<Core::Error> m_HeadError;
		std::optional<Core::Error> m_LogError;
		std::optional<Core::Error> m_WorkingTreeDiffError;

		// Kept apart from m_WorkingTreeDiffError: a commit diff and the volatile working-tree
		// diff are different query kinds, and one succeeding must not hide the other still
		// failing.
		std::optional<Core::Error> m_CommitDiffError;

		// Kept apart from m_CommitDiffError for the same reason: a file's texts and a
		// commit's diff are different query kinds, each with its own slot.
		std::optional<Core::Error> m_TextsError;
	};
}
