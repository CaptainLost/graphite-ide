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
#include <utility>
#include <vector>

namespace Graphite {

	using Core::JobSubmitter;

	struct DiscardRequest
	{
		std::vector<std::filesystem::path> Restore;
		std::vector<std::filesystem::path> Delete;
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
	// model and the queue behind its submitter must declare the model BEFORE the queue, so
	// the queue is destroyed and its workers joined first. The inline submitter used in tests
	// does not show a wrong order.
	class GitModel : Core::NonCopyable
	{
	public:
		GitModel(GitPort& port, JobSubmitter submit);

		void OnUpdate(Core::Timestep timestep);

		void RequestStatus();

		void RequestUntrackedIn(std::filesystem::path directory);

		Core::Ref<const Core::WorkingTreeStatus> GetUntrackedIn(
			const std::filesystem::path& directory) const;

		void RequestBranches();
		void RequestStashes();
		void RequestHead();
		// The commits a branch has that its base lacks, asked together with the log so that the
		// history can tell them from the rest. Label names the base for whoever draws the answer.
		struct OwnCommitsRequest
		{
			Core::LogRequest Query;
			std::string Label;
		};

		// WARNING: The own commits land in the same update as the log they were asked with. Landing
		// them apart shows the log for a frame without them, and a row appearing later moves the list.
		// A log asked without them clears the old ones as it lands, not when it is asked.
		void RequestLog(Core::LogRequest request, std::optional<OwnCommitsRequest> ownCommits = std::nullopt);

		void RequestWorkingTreeDiff(Core::DiffRequest request);

		void RequestDiffFor(const Core::Oid& commit);
		void ForgetFailedDiffFor(const Core::Oid& commit);
		bool IsDiffFailed(const Core::Oid& commit) const;
		Core::Ref<const Core::Diff> GetDiffFor(const Core::Oid& commit) const;

		static constexpr std::size_t kCachedDiffs = 32;

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

		static constexpr std::size_t kCachedTexts = 32;

		// WARNING: Status paths are relative to this directory, which is not always GetRoot():
		// opening a subdirectory of a repository resolves upward to the repository's working
		// directory. Joining a status path onto GetRoot() builds a path that does not exist.
		const std::filesystem::path& GetWorkingDirectory() const { return m_WorkingDirectory; }

		Core::Ref<const Core::WorkingTreeStatus> GetStatus() const { return m_Status.Get(); }
		Core::Ref<const std::vector<Core::Branch>> GetBranches() const { return m_Branches.Get(); }

		Core::Ref<const BranchAuthors> GetBranchAuthors() const { return m_BranchAuthors.Get(); }

		const std::optional<Core::Error>& GetBranchAuthorsError() const { return m_BranchAuthors.GetError(); }

		const std::optional<Core::Error>& GetBranchesError() const { return m_Branches.GetError(); }

		Core::Ref<const std::vector<Core::StashEntry>> GetStashes() const { return m_Stashes.Get(); }
		Core::Ref<const Core::HeadState> GetHead() const { return m_Head.Get(); }
		Core::Ref<const Core::LogPage> GetLog() const { return m_Log.Get(); }
		Core::Ref<const Core::LogPage> GetOwnCommits() const { return m_OwnLog.Get(); }
		const std::string& GetOwnCommitsLabel() const { return m_OwnLabel; }

		Core::Ref<const Core::Diff> GetWorkingTreeDiff() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeDiff.Get() : nullptr;
		}

		bool IsWorkingTreeDiffLoading() const
		{
			return m_WantedWorkingTreeDiff.has_value() && m_WorkingTreeDiffSettled != m_WantedWorkingTreeDiff;
		}

		Core::Ref<const FileTexts> GetWorkingTreeTexts() const
		{
			return AnswersWantedWorkingTreeDiff() ? m_WorkingTreeTexts : nullptr;
		}

		const std::optional<Core::Error>& GetStatusError() const { return m_Status.GetError(); }
		const std::optional<Core::Error>& GetLogError() const { return m_Log.GetError(); }
		const std::optional<Core::Error>& GetHeadError() const { return m_Head.GetError(); }
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

		bool HasOpenFolder() const { return !m_Root.empty(); }

		void PumpWorkingTreeDiff();

		void RequestBranchAuthorsWhenTipsMoved();

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

			bool Failed = false;
		};

		using UntrackedByDirectory = std::unordered_map<std::string, CachedUntracked>;

		CachedUntracked* FindUntracked(const std::string& directoryKey);
		const CachedUntracked* FindUntracked(const std::string& directoryKey) const;

		UntrackedByDirectory m_Untracked;

		// WARNING: Never restarts, not even in Clear(). An entry recreated after a Clear()
		// would otherwise let a delivery from before the clear outrank one from after it.
		std::uint64_t m_UntrackedGeneration = 0;

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

		std::filesystem::path m_Root;

		Debouncer m_Debounce;

		GitPort& m_Port;
		JobSubmitter m_Submit;

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

		std::optional<std::uint64_t> m_InitSettlesAfter;

		// WARNING: Fetched and applied together with m_Status, never lazily, so a status
		// snapshot never sits beside the previous folder's working directory.
		std::filesystem::path m_WorkingDirectory;

		GenerationSlot<Core::WorkingTreeStatus> m_Status;
		GenerationSlot<std::vector<Core::Branch>> m_Branches;
		GenerationSlot<BranchAuthors> m_BranchAuthors;
		std::optional<Core::OwnAuthorsRequest> m_LastAuthorsRequest;
		GenerationSlot<std::vector<Core::StashEntry>> m_Stashes;
		GenerationSlot<Core::HeadState> m_Head;
		GenerationSlot<Core::LogPage> m_Log;

		std::optional<Core::LogRequest> m_LastLogRequest;

		GenerationSlot<Core::LogPage> m_OwnLog;
		std::optional<OwnCommitsRequest> m_LastOwnRequest;
		std::string m_OwnLabel;

		GenerationSlot<Core::Diff> m_WorkingTreeDiff;
		Core::Ref<const FileTexts> m_WorkingTreeTexts;

		std::optional<Core::DiffRequest> m_WantedWorkingTreeDiff;
		std::optional<Core::DiffRequest> m_WorkingTreeDiffFor;

		// WARNING: Moves on a failure too, unlike m_WorkingTreeDiffFor. A failure must count as
		// an answer, or a permanent one would be retried every frame the tab stays open.
		std::optional<Core::DiffRequest> m_WorkingTreeDiffSettled;

		bool m_WorkingTreeDiffStale = false;

		LruResultCache<Core::Oid, Core::Diff> m_CommitDiffs{kCachedDiffs};
		LruResultCache<TextsKey, FileTexts, TextsKeyHash> m_Texts{kCachedTexts};

		std::optional<Core::Error> m_CommitDiffError;

		std::optional<Core::Error> m_TextsError;
	};
}
