#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Git/Debouncer.h"
#include "Git/DeliveryInbox.h"
#include "Git/FileTexts.h"
#include "Git/GenerationSlot.h"
#include "Git/GitPort.h"
#include "Git/LruResultCache.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

	// What happens to the working tree's own changes before a checkout runs over them.
	enum class CheckoutPreparation
	{
		None,
		Discard,
		Stash
	};

	struct CheckoutRequest
	{
		Core::CheckoutTarget Target;
		CheckoutPreparation Preparation = CheckoutPreparation::None;
		bool IncludeUntracked = false;

		// Used only when Preparation is Stash.
		std::string StashMessage;
	};

	// The answer to "may this branch go": how many commits only it reaches, which a delete
	// would leave without a name. A check that could not be made says so here rather than
	// through the write error, so the dialog waiting on it can tell its own failure from an
	// earlier write's.
	struct DeleteCheck
	{
		std::string Branch;
		std::size_t Unmerged = 0;
		std::optional<Core::Error> Failure;
	};

	// A plan, or why there is none, paired with the target it was asked about so a caller
	// can tell an answer from one it no longer wants. The failure travels here for the same
	// reason DeleteCheck's does.
	struct CheckoutPlanAnswer
	{
		Core::CheckoutTarget Target;
		std::optional<Core::CheckoutPlan> Plan;
		std::optional<Core::Error> Failure;
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
		Core::Ref<const Core::WorkingTreeStatus> GetUntrackedIn(
			const std::filesystem::path& directory) const;

		void RequestBranches();

		// A query like RequestBranches, refreshed beside it on every debounced refresh. Not
		// a write: listing the stashes changes nothing, so it need not wait its turn.
		void RequestStashes();

		// Refreshed alongside the status - see OnUpdate - so the bar's branch and its change
		// count never describe two different moments for longer than one query.
		void RequestHead();
		void RequestLog(Core::LogRequest request);

		// The working tree against HEAD: a diff that changes on every keystroke, so there is
		// nothing to hold past the next refresh. One slot, replaced whenever a fresher answer
		// lands, the same way m_Status and m_Branches are - unlike a commit's diff, which
		// never changes and is therefore kept in the keyed cache below (RequestDiffFor).
		//
		// Called every frame the diff window is open, so this registers the request the
		// window wants rather than submitting it. A query goes out only when the slot does
		// not already answer that request - a different request, or none yet - or when a
		// refresh (the same debounced one that re-asks the log) has run since the slot's
		// query was sent. One is in flight at a time; a request that changes meanwhile is
		// sent as soon as the one in flight lands.
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
		Core::Ref<const Core::Diff> GetDiffFor(const Core::Oid& commit) const;

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
		Core::Ref<const FileTexts> GetTextsFor(
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
		Core::Ref<const Core::WorkingTreeStatus> GetStatus() const { return m_Status.Get(); }
		Core::Ref<const std::vector<Core::Branch>> GetBranches() const { return m_Branches.Get(); }
		Core::Ref<const std::vector<Core::StashEntry>> GetStashes() const { return m_Stashes.Get(); }
		Core::Ref<const Core::HeadState> GetHead() const { return m_Head.Get(); }
		Core::Ref<const Core::LogPage> GetLog() const { return m_Log.Get(); }
		// Also null while the slot holds the answer to a different request than the one last
		// passed to RequestWorkingTreeDiff: a tab that follows its file to the other side asks
		// a new question, and the old side's hunks are not an answer to it.
		Core::Ref<const Core::Diff> GetWorkingTreeDiff() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeDiff.Get() : nullptr;
		}

		// True while the request last passed to RequestWorkingTreeDiff has not been answered
		// yet, by a success or a failure. Tells a null GetWorkingTreeDiff that is still on its
		// way apart from one whose query failed.
		bool IsWorkingTreeDiffLoading() const
		{
			return m_WantedWorkingTreeDiff.has_value() && m_WorkingTreeDiffSettled != m_WantedWorkingTreeDiff;
		}

		// The whole file's text on both sides of the working-tree diff above, fetched with it
		// and replaced with it. Null while none has arrived, and null for a file that is binary
		// or too large to lex - in every case the caller's answer is the same: draw the diff
		// without the colouring and the whole-file view that this text feeds. Null, too,
		// whenever GetWorkingTreeDiff is, for the same reason.
		Core::Ref<const FileTexts> GetWorkingTreeTexts() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeTexts : nullptr;
		}

		// The failure a panel should show, if any. Each query kind keeps its own error, set
		// by its own failure and cleared only by its own success, so a branches refresh
		// that succeeds can never hide a status refresh that is still failing. When more
		// than one is set this returns the first in the order status, branches, log,
		// working-tree diff, commit diff, file texts - status is checked most often and is
		// the one a user most needs to see fail, and a file-texts failure is the least
		// likely of the six to be what a user needs told first.
		// Each query's own failure, for a reader that has to tell one kind of failure from
		// another rather than show whichever came first (see HistoryPlaceholder).
		const std::optional<Core::Error>& GetStatusError() const { return m_Status.GetError(); }
		const std::optional<Core::Error>& GetLogError() const { return m_Log.GetError(); }

		// Not folded into GetLastError: the head is read only by the status bar, which draws
		// nothing rather than an error when it has no head, and the panels that show
		// GetLastError have their own status error to explain the same failure.
		const std::optional<Core::Error>& GetHeadError() const { return m_Head.GetError(); }

		// Not folded into GetLastError either: only the stash list reads it, and a failure
		// there says nothing about the changes the panel's main list shows.
		const std::optional<Core::Error>& GetStashesError() const { return m_Stashes.GetError(); }

		const std::optional<Core::Error>& GetLastError() const
		{
			if (m_Status.GetError().has_value())
			{
				return m_Status.GetError();
			}

			if (m_Branches.GetError().has_value())
			{
				return m_Branches.GetError();
			}

			if (m_Log.GetError().has_value())
			{
				return m_Log.GetError();
			}

			if (m_WorkingTreeDiff.GetError().has_value())
			{
				return m_WorkingTreeDiff.GetError();
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

		// Branch and stash writes. Every one of them queues behind the writes before it, the
		// plan and the delete check included: those answer about the tree as the writes
		// before them left it, not as it was when the button was drawn - so a plan asked for
		// right after a new branch finds that branch.
		void RequestCreateBranch(std::string name, Core::Oid from);
		void RequestRenameBranch(std::string from, std::string to);
		void RequestDeleteBranch(std::string branch);

		// Answered once through TakeDeleteCheck, failure included. Not counted as an applied
		// write, refreshes nothing, and never touches GetWriteError - neither to set it nor to
		// clear an earlier write's - since it changes nothing.
		void RequestDeleteCheck(std::string branch);
		std::optional<DeleteCheck> TakeDeleteCheck();

		void RequestStashSave(std::string message, bool includeUntracked);
		void RequestStashApply(Core::Oid id);
		void RequestStashPop(Core::Oid id);
		void RequestStashDrop(Core::Oid id);

		// Answered once through TakeCheckoutPlan, and otherwise treated exactly as the delete
		// check is.
		void RequestCheckoutPlan(Core::CheckoutTarget target);
		std::optional<CheckoutPlanAnswer> TakeCheckoutPlan();

		void RequestCheckout(CheckoutRequest request);

		// One-shot, like TakeDiscarded: every path the last checkout changed on disk - its own
		// list plus every path the preparation discarded or stashed. Relative to
		// GetWorkingDirectory(). Answered after a checkout that failed partway too, since the
		// files it did change still have to be reloaded.
		std::vector<std::filesystem::path> TakeCheckedOut();

		// For a panel that greys its buttons out. False does not mean nothing is pending —
		// it means nothing is happening at this instant.
		bool IsWriteInFlight() const { return m_WriteInFlight; }

		// True from a successful init until the first status asked for after it is applied.
		// Between the two the write is done but the status still says the folder is not a
		// repository, so a panel reading only IsWriteInFlight would offer the button again.
		bool IsAwaitingStatusAfterInit() const
		{
			return m_InitSettlesAfter.has_value() && m_Status.GetApplied() <= *m_InitSettlesAfter;
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
		static constexpr float kDebounceSeconds = Debouncer::kQuietSeconds;

		// A ceiling on how long coalescing can postpone the refresh, measured from the first
		// event of the burst rather than the most recent one. Without this, a sustained
		// stream of change events - a build writing into an output directory, which
		// Workspace::Update republishes per settled batch - would keep resetting the quiet
		// timer and starve the refresh for as long as the stream continues, rather than
		// coalescing to one refresh per quiet period.
		static constexpr float kMaxDebounceSeconds = Debouncer::kMaxSeconds;

	private:
		using Delivery = DeliveryInbox::Delivery;

		void Deliver(Delivery delivery);

		void MarkDirty();

		// Sends the wanted working-tree diff if nothing is in flight and the slot does not
		// already answer it fresh. Called on every ask and on every landing, which is what
		// sends a request that changed while another was in flight.
		void PumpWorkingTreeDiff();

		bool AnswersWantedWorkingTreeDiff() const
		{
			return m_WorkingTreeDiffFor.has_value() && m_WorkingTreeDiffFor == m_WantedWorkingTreeDiff;
		}

		// One commit touches many files, so the texts cache is keyed by commit and the path
		// that names the file together - the commit-diff cache needs only the commit, since it
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

		struct CachedUntracked
		{
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			Core::Ref<const Core::WorkingTreeStatus> Listing;

			bool Failed = false;
		};

		// Keyed by the path's UTF-8 string, because std::filesystem::path has no standard
		// hash. No LRU bound here, unlike kCachedDiffs: this map holds one entry per
		// directory the reader expanded since the last refresh, and every refresh empties
		// it, so there is nothing to accumulate.
		std::unordered_map<std::string, CachedUntracked> m_Untracked;

		// Model-wide and never restarts, for the reason LruResultCache spells out on its
		// entries' Requested: an entry recreated after a Clear() would otherwise let a
		// pre-clear delivery outrank a post-clear one.
		std::uint64_t m_UntrackedGeneration = 0;

		enum class WriteKind
		{
			Stage,
			Unstage,
			Discard,
			Commit,
			Init,
			CreateBranch,
			RenameBranch,
			DeleteCheck,
			DeleteBranch,
			StashSave,
			StashApply,
			StashPop,
			StashDrop,
			CheckoutPlan,
			Checkout
		};

		// One queued write. Carries every kind's payload rather than a variant: there are
		// fifteen kinds sharing a handful of small fields, and a variant here would cost more
		// to read than the fields it would save.
		struct WriteJob
		{
			WriteKind Kind = WriteKind::Stage;
			std::vector<std::filesystem::path> Paths;
			DiscardRequest Discard;
			Core::CommitRequest Commit;

			// A branch name or a stash message; OtherName is a rename's new name.
			std::string Name;
			std::string OtherName;

			// A stash's id, or the commit a new branch starts at.
			Core::Oid Id;

			// A stash's include-untracked.
			bool Flag = false;

			// A plan's target.
			std::optional<Core::CheckoutTarget> Target;
			CheckoutRequest Checkout;

			// The generation this was queued under. Unlike a query's generation this never
			// orders two results — there is no such thing as a stale commit — it only says
			// whether the folder it was meant for is still open.
			std::uint64_t Generation = 0;
		};

		// What a write came back with. For a commit, an id or an error, never both. A discard
		// or a checkout can be both halves of a result: Changed names the paths it did touch,
		// and Failure the step that then stopped it.
		struct WriteOutcome
		{
			std::optional<Core::Error> Failure;
			std::optional<Core::Oid> Committed;
			std::vector<std::filesystem::path> Changed;
			std::optional<Core::CheckoutPlan> Plan;
			std::optional<std::size_t> Unmerged;
		};

		void Enqueue(WriteJob job);

		// Starts the next queued write if none is running. Called after every enqueue and
		// after every completion, which is what keeps the queue moving without a timer.
		void PumpWrites();

		// Runs on a worker thread. Touches m_Port and nothing else on this object.
		WriteOutcome RunWrite(const WriteJob& job);

		// The checkout job's steps - note the changed paths, prepare, check out. Runs on the
		// worker inside RunWrite's try.
		WriteOutcome RunCheckout(const CheckoutRequest& request);

		// Runs on the UI thread, out of the inbox, like every other delivery.
		void ApplyWrite(const WriteJob& job, WriteOutcome outcome);

		std::filesystem::path m_Root;

		Debouncer m_Debounce;

		GitPort& m_Port;
		JobSubmitter m_Submit;

		// Written by workers, drained by OnUpdate. The mutex covers this and nothing else:
		// every snapshot below is only ever touched on the UI thread — the write queue's
		// state included, since PumpWrites, ApplyWrite and Enqueue all run there too.
		DeliveryInbox m_Inbox;

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
		std::vector<std::filesystem::path> m_CheckedOut;
		std::optional<CheckoutPlanAnswer> m_CheckoutPlan;
		std::optional<DeleteCheck> m_DeleteCheck;
		std::uint64_t m_AppliedWrites = 0;

		// The last status generation requested when an init was applied. A status asked for
		// before then - even one landing afterwards - still describes the folder without a
		// repository, so only one numbered past this settles it.
		std::optional<std::uint64_t> m_InitSettlesAfter;

		// See GetWorkingDirectory(): fetched and applied alongside m_Status, never lazily.
		std::filesystem::path m_WorkingDirectory;

		// Each query kind's snapshot, generations and error together - see GenerationSlot. The
		// errors are per slot so a branches refresh that succeeds can never hide a status
		// refresh that is still failing.
		GenerationSlot<Core::WorkingTreeStatus> m_Status;
		GenerationSlot<std::vector<Core::Branch>> m_Branches;
		GenerationSlot<std::vector<Core::StashEntry>> m_Stashes;
		GenerationSlot<Core::HeadState> m_Head;
		GenerationSlot<Core::LogPage> m_Log;

		// The last question asked about the history, so a refresh can ask it again. Unset
		// until something asks for a log, and dropped by Clear() with the rest of the
		// folder's state - see RequestLog and OnUpdate for both halves of the reasoning.
		std::optional<Core::LogRequest> m_LastLogRequest;

		// Replaced on every landing like the slots above, and also driven by the wanted,
		// settled and stale state below - see PumpWorkingTreeDiff.
		GenerationSlot<Core::Diff> m_WorkingTreeDiff;
		Core::Ref<const FileTexts> m_WorkingTreeTexts;

		// The request the diff window last asked about, and the one m_WorkingTreeDiff and
		// m_WorkingTreeTexts answer. They differ between a tab changing its question and the
		// new answer landing, and the getters answer null for that long.
		std::optional<Core::DiffRequest> m_WantedWorkingTreeDiff;
		std::optional<Core::DiffRequest> m_WorkingTreeDiffFor;

		// The request the last landed query answered, success or failure. Kept apart from
		// m_WorkingTreeDiffFor, which only a success moves: a failure must count as an answer
		// too, or a permanent one would be retried every frame the tab stays open.
		std::optional<Core::DiffRequest> m_WorkingTreeDiffSettled;

		// Set by every debounced refresh and cleared when a query is sent, so a refresh that
		// runs while a query is in flight still asks again once it lands: that query may have
		// read the file before the change that caused the refresh.
		bool m_WorkingTreeDiffStale = false;

		// See RequestDiffFor and RequestTextsFor for what each holds and why it is never refreshed.
		LruResultCache<Core::Oid, Core::Diff> m_CommitDiffs{kCachedDiffs};
		LruResultCache<TextsKey, FileTexts, TextsKeyHash> m_Texts{kCachedTexts};

		// Kept apart from m_WorkingTreeDiff's error: a commit diff and the volatile working-tree
		// diff are different query kinds, and one succeeding must not hide the other still
		// failing.
		std::optional<Core::Error> m_CommitDiffError;

		// Kept apart from m_CommitDiffError for the same reason: a file's texts and a
		// commit's diff are different query kinds, each with its own slot.
		std::optional<Core::Error> m_TextsError;
	};
}
