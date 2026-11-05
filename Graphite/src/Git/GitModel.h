#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/OwnAuthors.h"
#include "Core/Jobs/JobSubmitter.h"
#include "Git/Debouncer.h"
#include "Git/Branches/BranchAuthors.h"
#include "Git/DeliveryInbox.h"
#include "Git/Diff/FileTexts.h"
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
#include <unordered_set>
#include <utility>
#include <vector>

namespace Graphite {

	using Core::JobSubmitter;

	struct DiscardRequest
	{
		std::vector<std::filesystem::path> Restore;
		std::vector<std::filesystem::path> Delete;

		// WARNING: Every file the discard rewrites, listed when it was asked, because a deleted folder is gone from disk by
		// the time it lands and the editor holds its files, not the folder. Empty when nothing was listed.
		std::vector<std::filesystem::path> ListedFiles;
	};

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
		std::string StashMessage;
	};

	struct DeleteCheck
	{
		std::string Branch;
		std::size_t Unmerged = 0;
		std::optional<Core::Error> Failure;
	};

	struct CheckoutPlanAnswer
	{
		Core::CheckoutTarget Target;
		std::optional<Core::CheckoutPlan> Plan;
		std::optional<Core::Error> Failure;
	};

	// WARNING: Every job submitted by the model captures `this`. An owner holding both the
	// model and the queues behind its submitters must declare the model BEFORE the queues, so
	// the queues are destroyed and their workers joined first. The inline submitter used in tests
	// does not show a wrong order.
	class GitModel : Core::NonCopyable
	{
	public:
		GitModel(GitPort& port, JobSubmitter submit);

		GitModel(GitPort& port, JobSubmitter submitRead, JobSubmitter submitWrite);

		void OnUpdate(Core::Timestep timestep);

		void RequestStatus();

		void RequestUntrackedIn(std::filesystem::path directory);

		// WARNING: A listing is kept through the status refreshes that still list its folder, and asked for again by
		// the next RequestUntrackedIn, so the one returned can be a refresh behind while that answer is on its way.
		Core::Ref<const Core::WorkingTreeStatus> GetUntrackedIn(
			const std::filesystem::path& directory) const;

		std::optional<Core::Error> GetUntrackedFailureIn(const std::filesystem::path& directory) const;

		// WARNING: False while a kept listing waits to be read again after a status, when its rows may name files
		// already staged or gone, so their buttons must not act on them.
		bool IsUntrackedListingCurrent(const std::filesystem::path& directory) const;

		void RequestBranches();
		void RequestStashes();
		void RequestHead();
		enum class MarkedKind
		{
			Own,  // the commits a branch has that its line lacks
			Newer // the commits its line has that the branch lacks
		};

		// Commits asked together with the log so that the history can tell them from the rest. Label names the line for
		// whoever draws the answer.
		struct MarkedCommitsRequest
		{
			Core::LogRequest Query;
			std::string Label;
			MarkedKind Kind = MarkedKind::Own;
		};

		// WARNING: The marked commits land in the same update as the log they were asked with. Landing
		// them apart shows the log for a frame without them, and a row appearing later moves the list.
		// A log asked without them clears the old ones as it lands, not when it is asked.
		void RequestLog(Core::LogRequest request, std::optional<MarkedCommitsRequest> marked = std::nullopt);

		void RequestWorkingTreeDiff(Core::DiffRequest request);

		void RequestDiffFor(const Core::Oid& commit);
		void ForgetFailedDiffFor(const Core::Oid& commit);
		bool IsDiffFailed(const Core::Oid& commit) const;

		std::optional<Core::Error> GetDiffFailure(const Core::Oid& commit) const;

		bool HasOpenFolder() const { return !m_Root.empty(); }
		Core::Ref<const Core::Diff> GetDiffFor(const Core::Oid& commit) const;

		static constexpr std::size_t kCachedDiffs = 32;

		static constexpr std::size_t kCachedBrought = 16;

		// Reads the commits a merge brought: its second parent's first-parent chain down to what the first parent had.
		void RequestBrought(const Core::Oid& merge, const Core::Oid& firstParent, const Core::Oid& secondParent);

		Core::Ref<const Core::LogPage> GetBrought(const Core::Oid& merge) const { return m_Brought.Get(merge); }

		std::optional<Core::Error> GetBroughtFailure(const Core::Oid& merge) const { return m_Brought.FailureOf(merge); }

		// A failed read is asked again by the next RequestBrought.
		void ForgetFailedBrought(const Core::Oid& merge) { m_Brought.ForgetFailed(merge); }

		// The merges whose brought commits are never let go of, however many: the merges open in the history, whose rows
		// would otherwise come and go as they are read again.
		void KeepBroughtOf(std::unordered_set<Core::Oid> merges) { m_Brought.SetKept(std::move(merges)); }

		// WARNING: `key` is the path that names the file, never `oldPath` or `newPath`: either
		// may be empty to say a side is absent, so every file deleted in one commit would share
		// one key, and so would every file added.
		void RequestTextsFor(
			const Core::Oid& commit,
			const std::filesystem::path& key,
			const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath);

		void ForgetFailedTextsFor(const Core::Oid& commit, const std::filesystem::path& key);

		Core::Ref<const FileTexts> GetTextsFor(
			const Core::Oid& commit, const std::filesystem::path& key) const;

		bool IsTextsFailed(const Core::Oid& commit, const std::filesystem::path& key) const;

		std::optional<Core::Error> GetTextsFailure(const Core::Oid& commit, const std::filesystem::path& key) const;

		static constexpr std::size_t kCachedTexts = 32;

		// WARNING: Status paths are relative to this directory, which is not always GetRoot():
		// opening a subdirectory of a repository resolves upward to the repository's working
		// directory. Joining a status path onto GetRoot() builds a path that does not exist.
		const std::filesystem::path& GetWorkingDirectory() const { return m_WorkingDirectory; }

		Core::Ref<const Core::WorkingTreeStatus> GetStatus() const { return m_Status.Get(); }
		Core::Ref<const std::vector<Core::Branch>> GetBranches() const { return m_Branches.Get(); }

		Core::Ref<const BranchAuthors> GetBranchAuthors() const { return m_BranchAuthors.Get(); }

		const std::optional<Core::Error>& GetBranchAuthorsError() const { return m_BranchAuthors.GetError(); }

		bool IsBranchAuthorsInFlight() const { return m_BranchAuthors.IsInFlight(); }

		// The tips "own" authors leave out, the tips of the line read's bases (BaseTipsOf). The authors are asked again
		// when they or the branch tips changed. WARNING: Until it is first called the authors are not asked at all, since
		// with nothing hidden every commit would count as its own.
		void SetAuthorsHidden(std::vector<Core::Oid> baseTips);

		const std::optional<Core::Error>& GetBranchesError() const { return m_Branches.GetError(); }

		// A branch listing is being read, or a refresh that reads one is due, as after this window's own checkout, whose
		// HEAD is read at once and whose branches only with the refresh it marks.
		bool IsBranchListingComing() const { return m_Branches.IsInFlight() || m_Debounce.IsDirty(); }

		// Moved by every branch listing that lands, the same listing again too, as each refresh lands one.
		std::uint64_t GetBranchListingGeneration() const { return m_Branches.GetApplied(); }

		// Read beside every branch listing, so what needs it never waits for the authors walk. Empty while unread and
		// when git has none.
		const std::optional<std::string>& GetUserEmail() const;

		// Read beside every branch listing, for the window bar. Null while unread and after a failed read.
		Core::Ref<const std::vector<Core::Remote>> GetRemotes() const { return m_Remotes.Get(); }
		const std::optional<Core::Error>& GetRemotesError() const { return m_Remotes.GetError(); }

		Core::Ref<const std::vector<Core::StashEntry>> GetStashes() const { return m_Stashes.Get(); }
		Core::Ref<const Core::HeadState> GetHead() const { return m_Head.Get(); }
		Core::Ref<const Core::LogPage> GetLog() const { return m_Log.Get(); }

		// The request the log on screen answers: set when a page lands, never by a failed or stale
		// answer, so it always describes the page GetLog returns.
		const std::optional<Core::LogRequest>& GetShownLogRequest() const { return m_ShownLogRequest; }

		// The request last asked for, by the reader or by a refresh, whether or not it has answered: what
		// the query running reads while the page on screen still answers an earlier one.
		const std::optional<Core::LogRequest>& GetAskedLogRequest() const { return m_LastLogRequest; }

		// The generation of the last log asked for, and whether the log has answered it, with a page
		// or an error. An answer to a later ask also answers an earlier one.
		std::uint64_t GetLogGeneration() const { return m_Log.GetRequested(); }
		bool IsLogAnswered(std::uint64_t generation) const { return m_Log.GetApplied() >= generation; }

		// The marked commits that landed with the log on screen, what they are, and the line they were asked against.
		Core::Ref<const Core::LogPage> GetMarkedCommits() const { return m_MarkedLog.Get(); }
		std::optional<MarkedKind> GetMarkedKind() const;
		const std::string& GetMarkedLabel() const { return m_MarkedLabel; }

		// The length of the history a log walks, counted for exactly `ref` along exactly `parents`: a
		// count of the branch shown before never stands beside another branch's page, and a count of
		// every commit never stands beside a first-parent history's rows.
		std::optional<std::size_t> GetCommitCountFor(const std::optional<std::string>& ref, Core::FollowedParents parents) const;
		bool IsCommitCountInFlight() const { return m_CommitCount.IsInFlight(); }

		Core::Ref<const Core::Diff> GetWorkingTreeDiff() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeDiff.Get() : nullptr;
		}

		bool IsWorkingTreeDiffLoading() const
		{
			return m_WantedWorkingTreeDiff.has_value() && m_WorkingTreeDiffSettled != m_WantedWorkingTreeDiff;
		}

		std::optional<Core::Error> GetWorkingTreeDiffFailure() const;

		void RetryWorkingTreeDiff();

		Core::Ref<const FileTexts> GetWorkingTreeTexts() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeTexts : nullptr;
		}

		std::optional<Core::Error> GetWorkingTreeTextsFailure() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeTextsFailure : std::nullopt;
		}

		const std::optional<Core::Error>& GetStatusError() const { return m_Status.GetError(); }
		const std::optional<Core::Error>& GetLogError() const { return m_Log.GetError(); }
		const std::optional<Core::Error>& GetHeadError() const { return m_Head.GetError(); }
		const std::optional<Core::Error>& GetStashesError() const { return m_Stashes.GetError(); }

		void RequestStage(std::vector<std::filesystem::path> paths);
		void RequestUnstage(std::vector<std::filesystem::path> paths);
		void RequestDiscard(DiscardRequest request);
		void RequestCommit(Core::CommitRequest request);

		void RequestInit();

		void RequestCreateBranch(std::string name, Core::Oid from);
		void RequestRenameBranch(std::string from, std::string to);
		void RequestDeleteBranch(std::string branch);

		void RequestDeleteCheck(std::string branch);
		std::optional<DeleteCheck> TakeDeleteCheck();

		void RequestStashSave(std::string message, bool includeUntracked);
		void RequestStashApply(Core::Oid id);
		void RequestStashPop(Core::Oid id);
		void RequestStashDrop(Core::Oid id);

		void RequestCheckoutPlan(Core::CheckoutTarget target);
		std::optional<CheckoutPlanAnswer> TakeCheckoutPlan();

		void RequestCheckout(CheckoutRequest request);

		std::vector<std::filesystem::path> TakeCheckedOut();

		bool IsWriteInFlight() const { return m_WriteInFlight; }

		bool IsAwaitingStatusAfterInit() const
		{
			return m_InitSettlesAfter.has_value() && m_Status.GetApplied() <= *m_InitSettlesAfter;
		}

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

		struct WriteFailure
		{
			WriteKind Kind = WriteKind::Stage;
			Core::Error Error;
		};

		std::optional<WriteFailure> TakeWriteError();

		struct WriteSubject
		{
			WriteKind Kind = WriteKind::Stage;

			std::size_t PathCount = 0;
			std::string OnePathName;

			std::string Name;
			std::string NewName;

			std::optional<std::size_t> StashIndex;

			std::optional<Core::Oid> Committed;
			std::string CommitSummary;

			friend bool operator==(const WriteSubject&, const WriteSubject&) = default;
		};

		enum class WriteProgress
		{
			Running,

			Done
		};

		struct WriteReport
		{
			WriteSubject Subject;
			WriteProgress Progress = WriteProgress::Running;
		};

		static constexpr float kWriteConfirmationSeconds = 4.0f;

		// WARNING: A failed write leaves no report, since its toast says what happened.
		std::optional<WriteReport> GetWriteReport() const;

		// WARNING: True until the status read after a write lands, not only while the write runs, since the rows
		// drawn before that read are out of date.
		bool IsWriteUnsettled() const;

		bool IsUpdatingWorkingTree() const;

		std::optional<Core::CheckoutTarget> GetCheckoutUnderWay() const;

		const std::optional<Core::Error>& GetInitError() const { return m_InitError; }
		void DismissInitError() { m_InitError.reset(); }

		std::optional<Core::Oid> TakeCommitted();

		std::vector<std::filesystem::path> TakeDiscarded();

		std::uint64_t GetAppliedWriteCount() const { return m_AppliedWrites; }

		void SetRoot(std::filesystem::path root);

		void Clear();

		void SubscribeTo(Core::EventBus& events);

	private:
		using Delivery = DeliveryInbox::Delivery;

		void Deliver(Delivery delivery);

		void MarkDirty();

		void RequestStatusOn(const JobSubmitter& submit);
		void RequestHeadOn(const JobSubmitter& submit);

		// WARNING: On the write queue, so the read that settles a write never waits behind Load all, the authors walk
		// or a commit count on the shared one.
		void ReadStatusAfterWrite();


		void PumpWorkingTreeDiff();

		void RequestBranchAuthorsWhenTipsMoved();

		void RequestUserEmail();
		void RequestRemotes();

		bool AnswersWantedWorkingTreeDiff() const
		{
			return m_WorkingTreeDiffFor.has_value() && m_WorkingTreeDiffFor == m_WantedWorkingTreeDiff;
		}

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

			std::optional<Core::Error> Failure;

			bool Stale = false;
		};

		using UntrackedByDirectory = std::unordered_map<std::string, CachedUntracked>;

		CachedUntracked* FindUntracked(const std::string& directoryKey);
		const CachedUntracked* FindUntracked(const std::string& directoryKey) const;

		void KeepUntrackedListingsOf(const Core::WorkingTreeStatus& status);

		UntrackedByDirectory m_Untracked;

		// WARNING: Never restarts, not even in Clear(). An entry recreated after a Clear()
		// would otherwise let a delivery from before the clear outrank one from after it.
		std::uint64_t m_UntrackedGeneration = 0;

		static constexpr std::uint64_t kNoTicket = 0;

		struct WriteJob
		{
			WriteKind Kind = WriteKind::Stage;
			std::vector<std::filesystem::path> Paths;
			DiscardRequest Discard;
			Core::CommitRequest Commit;

			std::string Name;
			std::string OtherName;

			Core::Oid Id;

			bool Flag = false;

			std::optional<Core::CheckoutTarget> Target;
			CheckoutRequest Checkout;

			std::uint64_t Generation = 0;

			std::uint64_t Ticket = kNoTicket;
		};

		struct WriteOutcome
		{
			std::optional<Core::Error> Failure;
			std::optional<Core::Oid> Committed;
			std::vector<std::filesystem::path> Changed;
			std::optional<Core::CheckoutPlan> Plan;
			std::optional<std::size_t> Unmerged;
		};

		void Enqueue(WriteJob job);

		void PumpWrites();

		// WARNING: Runs on a worker thread. Touches m_Port and nothing else on this object.
		WriteOutcome RunWrite(const WriteJob& job);

		WriteOutcome RunCheckout(const CheckoutRequest& request);

		void ApplyWrite(const WriteJob& job, WriteOutcome outcome);

		enum class TicketPhase
		{
			Queued,
			Running,
			Applied
		};

		struct WriteTicket
		{
			std::uint64_t Number = kNoTicket;
			WriteSubject Subject;
			std::optional<Core::CheckoutTarget> Checkout;

			TicketPhase Phase = TicketPhase::Queued;

			// WARNING: Never reported, yet a write that changed files before it failed still waits for the status after it.
			bool Failed = false;

			// WARNING: Only an answer to a later ask than these was read after the write.
			std::uint64_t StatusAskedWhenApplied = 0;
			std::uint64_t HeadAskedWhenApplied = 0;
		};

		WriteSubject DescribeWrite(const WriteJob& job) const;

		std::optional<std::size_t> StashIndexOf(const Core::Oid& stash) const;

		WriteTicket* FindTicket(std::uint64_t number);

		const WriteTicket* RunningTicket() const;

		const WriteTicket* NewestUnfailedTicket() const;

		void ForgetTicket(std::uint64_t number);

		void MarkTicketRunning(std::uint64_t number);

		void AwaitStatusAfter(std::uint64_t number, const std::optional<Core::Oid>& committed);

		void FailTicket(std::uint64_t number, bool changedRepository);

		bool HasSettled(const WriteTicket& ticket) const;

		void SettleTicketsWhoseStatusLanded();

		void CountDownConfirmation(Core::Timestep timestep);

		std::filesystem::path m_Root;

		Debouncer m_Debounce;

		GitPort& m_Port;
		JobSubmitter m_Submit;
		JobSubmitter m_SubmitWrite;

		// WARNING: The inbox's mutex covers the inbox and nothing else. Every other member is
		// touched on the UI thread only, the write queue's state included.
		DeliveryInbox m_Inbox;

		std::deque<WriteJob> m_WriteQueue;
		bool m_WriteInFlight = false;

		std::uint64_t m_WriteGeneration = 0;

		std::deque<WriteFailure> m_WriteErrors;
		std::optional<Core::Error> m_InitError;
		std::optional<Core::Oid> m_Committed;
		std::vector<std::filesystem::path> m_Discarded;
		std::vector<std::filesystem::path> m_CheckedOut;
		std::optional<CheckoutPlanAnswer> m_CheckoutPlan;
		std::optional<DeleteCheck> m_DeleteCheck;
		std::uint64_t m_AppliedWrites = 0;

		std::deque<WriteTicket> m_Tickets;
		std::uint64_t m_LastTicket = kNoTicket;

		std::optional<WriteSubject> m_Confirmed;
		float m_ConfirmedSecondsLeft = 0.0f;

		std::optional<std::uint64_t> m_InitSettlesAfter;

		// WARNING: Fetched and applied together with m_Status, never lazily, so a status
		// snapshot never sits beside the previous folder's working directory.
		std::filesystem::path m_WorkingDirectory;

		GenerationSlot<Core::WorkingTreeStatus> m_Status;
		GenerationSlot<std::vector<Core::Branch>> m_Branches;
		GenerationSlot<BranchAuthors> m_BranchAuthors;
		GenerationSlot<std::optional<std::string>> m_UserEmail;
		GenerationSlot<std::vector<Core::Remote>> m_Remotes;
		std::optional<Core::OwnAuthorsRequest> m_LastAuthorsRequest;
		std::optional<std::vector<Core::Oid>> m_AuthorsHidden;
		GenerationSlot<std::vector<Core::StashEntry>> m_Stashes;
		GenerationSlot<Core::HeadState> m_Head;
		GenerationSlot<Core::LogPage> m_Log;

		std::optional<Core::LogRequest> m_LastLogRequest;
		std::optional<Core::LogRequest> m_ShownLogRequest;

		// A refresh came while a log was being read; it is asked once no log is in flight any more.
		// Only ever set while m_LastLogRequest holds the request to ask again.
		bool m_LogRefreshPending = false;

		void LandMarkedCommits(std::uint64_t generation, std::optional<Core::Result<Core::LogPage>> result,
							   std::string label, MarkedKind kind);

		void AskHeldLogRefresh();

		struct RefCommitCount
		{
			std::optional<std::string> Ref;
			Core::FollowedParents Parents = Core::FollowedParents::All;
			CommitCount Count;
		};

		// WARNING: Asked with every log, the model's own refreshes included, and handed the count it
		// holds for the same ref and parents, so the port walks only when that ref's tip has moved.
		void RequestCommitCount(const std::optional<std::string>& ref, Core::FollowedParents parents);

		GenerationSlot<RefCommitCount> m_CommitCount;

		GenerationSlot<Core::LogPage> m_MarkedLog;
		std::optional<MarkedCommitsRequest> m_LastMarkedRequest;
		std::string m_MarkedLabel;
		MarkedKind m_MarkedKind = MarkedKind::Own; // meaningful while m_MarkedLog holds a page

		GenerationSlot<Core::Diff> m_WorkingTreeDiff;
		Core::Ref<const FileTexts> m_WorkingTreeTexts;
		std::optional<Core::Error> m_WorkingTreeTextsFailure;

		std::optional<Core::DiffRequest> m_WantedWorkingTreeDiff;
		std::optional<Core::DiffRequest> m_WorkingTreeDiffFor;

		// WARNING: Moves on a failure too, unlike m_WorkingTreeDiffFor. A failure must count as
		// an answer, or a permanent one would be retried every frame the tab stays open.
		std::optional<Core::DiffRequest> m_WorkingTreeDiffSettled;

		bool m_WorkingTreeDiffStale = false;

		LruResultCache<Core::Oid, Core::Diff> m_CommitDiffs{kCachedDiffs};
		LruResultCache<Core::Oid, Core::LogPage> m_Brought{kCachedBrought};
		LruResultCache<TextsKey, FileTexts, TextsKeyHash> m_Texts{kCachedTexts};
	};
}
