#pragma once

#include "Core/Base/Base.h"

#include "Git/Content/ContentMark.h"
#include "Git/Content/ContentPort.h"
#include "Git/Content/ContentStore.h"
#include "Git/Content/ContentWriter.h"
#include "Git/Content/RememberedVerdicts.h"
#include "Git/DeliveryInbox.h"
#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/Content/ContentCheck.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineModel.h"
#include "Core/Git/Oid.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Jobs/JobSubmitter.h"

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// The branch a commit's change came from: the commit is the landing of the branch's squash, or the copy of one of the
	// branch's own commits.
	struct ContentSource
	{
		std::string Branch;                                 // full name
		Core::ContentKind Kind = Core::ContentKind::Squash; // Squash for a landing, Rebase for a copy
		std::optional<Core::Oid> Own;                       // the branch's commit a copy is of; empty for a landing

		friend bool operator==(const ContentSource&, const ContentSource&) = default;
	};

	// Checks every branch of the followed read that history does not show merged into its line for a merge by content:
	// one check at a time on the shared queue, only while the Git window is on screen, the shown branch and the rows in
	// view first. A branch found merged against an earlier line tip stays merged, unchecked, while the line still holds the
	// squash's landing or every copy of the rebase: the line's later edits to the same lines would make a new check say
	// no. The verdicts and the patch ids the checks read are kept in the cache files of the read's line store, written at
	// most once every kSecondsBetweenSaves. UI thread only, but for the jobs it submits.
	// WARNING: Every job captures `this`; the owner declares this before the queue behind the submitter.
	class ContentModel : Core::NonCopyable
	{
	public:
		ContentModel(ContentPort& port, Core::JobSubmitter submit);

		// Lands what finished, starts the next check while on screen and nothing runs, and saves once a save is due.
		void OnUpdate(Core::Timestep timestep);

		// Loads the cache files of the read's line store the first time it is followed, and checks the read's branches
		// from then on.
		void Follow(Core::Ref<const LineRead> read);

		void SetOnScreen(bool onScreen);

		// Saves what changed into the store it was found for, and forgets everything else: a check of the folder before
		// that lands later is dropped.
		void Clear();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Clear()

		// For a row drawn this frame: the rows in view are checked first. Known with NotMerged for a branch that is not a
		// candidate. The line must be one of the followed read's.
		ContentMark MarkOf(const Core::BranchLine& line);

		// The same, for the branch whose history is shown, which is checked before every other.
		ContentMark MarkOfShown(const Core::BranchLine& line);

		// What is known now, asking nothing.
		ContentMark KnownMarkOf(const Core::BranchLine& line) const;

		// From the known verdicts of the followed read's branches: the first, in the order they are checked, whose
		// landing or copy `commit` is.
		std::optional<ContentSource> SourceOf(const Core::Oid& commit) const;

		// Searched only while the window is on screen, in slices of kPatchCommitsPerCopySlice more line commits each, up to
		// kPatchCommitsPerCheck: the first slice is asked like the shown branch, before the rows in view; each later one
		// after the shown branch's check and the rows in view, so a branch opened meanwhile waits for one slice, not the
		// search. Pending until the last slice answers, which answers as one search over the whole budget would.
		TwinMark TwinIn(const Core::Oid& commit, const Core::Oid& lineTip);

		// A check or a copy search that failed, once each: to the log, unless a test listens instead.
		std::function<void(const std::string& message)> LogFailure;

		static constexpr float kSecondsBetweenSaves = 5.0f;

	private:
		struct TwinKey
		{
			Core::Oid Commit;
			Core::Oid LineTip;

			friend auto operator<=>(const TwinKey&, const TwinKey&) = default;
		};

		void Deliver(DeliveryInbox::Delivery delivery);
		void Submit(std::function<void()> job);
		void ReportFailure(const std::string& message) const;

		// Read on worker threads: a job of a folder cleared since it was asked does nothing.
		bool IsSameFolder(std::uint64_t folder) const;

		// The rows in view and the shown branch asked in the last frame, of the read before, found again by name: the
		// first check after a new read is theirs, as every other.
		void CarryAsksTo(const Core::Ref<const LineRead>& next);

		void LoadStore(const LineStorePaths& store);
		void IndexBranches();
		void FindVerdictsToRecheck();
		void RebuildSources();
		void AddSourcesOf(const std::string& branch, const Core::ContentVerdict& verdict);

		std::optional<std::size_t> BranchIndexOf(const Core::BranchLine& line) const;
		std::optional<ContentKey> KeyOf(const Core::BranchLine& line) const;
		bool IsWaiting(const ContentKey& key) const;
		bool IsWaitingBranch(std::size_t branch) const;
		std::optional<std::size_t> FirstWaitingBranch(const std::vector<std::size_t>& order) const;
		// The first asked search not answered and not running: one not started yet, or one with slices already read.
		std::optional<TwinKey> FirstTwinWaiting(const std::vector<TwinKey>& asked, bool started) const;
		bool IsTwinAsked(const TwinKey& key) const;

		void StartNext(const std::set<std::size_t>& inView, std::optional<std::size_t> shown, const std::vector<TwinKey>& twinsAsked);
		void StartRecheck();
		void StartCheck(std::size_t branch);
		void StartTwin(const TwinKey& key);

		void LandRecheck(const std::vector<RememberedVerdict>& held);
		void LandCheck(const ContentKey& key, const std::string& lineName, const Core::Result<Core::ContentVerdict>& verdict,
					   bool patchIdsGrew);
		void LandTwin(const TwinKey& key, std::size_t budget, const Core::Result<Core::PatchTwinSearch>& twin, bool patchIdsGrew);

		void SaveWhenDue();
		void SaveChanges();

		ContentPort& m_Port;
		Core::JobSubmitter m_Submit;

		// WARNING: The inbox's mutex covers the inbox and nothing else; every member from here to the writer is touched on
		// the UI thread only.
		DeliveryInbox m_Inbox;

		Core::Ref<const LineRead> m_Read;
		std::vector<std::optional<std::size_t>> m_BranchOfRef; // per ref of m_Read: its place in Model.Branches

		std::optional<LineStorePaths> m_Store; // the store whose files are loaded
		ContentCache m_Cache;
		Core::Ref<PatchIdStore> m_PatchIds;

		// One job runs at a time: a check takes up to a second, and the lines read and the log share the queue.
		bool m_Running = false;
		std::optional<ContentKey> m_Checking;
		std::optional<TwinKey> m_TwinChecking;

		std::set<ContentKey> m_Failed;          // the check failed: not saved, asked again in the next session
		std::set<ContentKey> m_RecheckWaiting;  // found merged against another line tip; does this one hold it?
		std::set<ContentKey> m_RecheckRunning;
		bool m_EveryCandidateAsked = false;     // of the followed read

		std::map<TwinKey, TwinMark> m_Twins;    // answered
		std::map<TwinKey, std::size_t> m_TwinsReadSoFar; // searches with slices read and no answer yet: the budget read

		// Asked since the last update.
		std::set<std::size_t> m_InView;
		std::optional<std::size_t> m_Shown;
		std::vector<TwinKey> m_TwinsAsked;

		std::unordered_map<Core::Oid, ContentSource> m_Sources;

		bool m_OnScreen = false;
		double m_Clock = 0.0; // seconds of OnUpdate time
		double m_LastSaveAt = 0.0;
		bool m_VerdictsChanged = false;
		bool m_PatchIdsChanged = false;
		std::uint64_t m_SaveSequence = 0;

		ContentWriter m_Writer; // used by the save jobs

		// Submitted and not yet returned, of any folder: one job runs at a time across a folder switch too.
		std::atomic<std::size_t> m_UnfinishedJobs{0};

		// WARNING: Bumped by Clear, which the folder events run before the port is pointed at the next folder, and read by
		// every job as it starts and every delivery as it lands: a check asked for the folder before must not land in the
		// next folder's verdicts or patch ids. A job stalled between that check and the port opening its repository is
		// refused by the port.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
