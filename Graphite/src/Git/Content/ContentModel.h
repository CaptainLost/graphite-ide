#pragma once

#include "Core/Base/Base.h"

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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	// What is known about whether a branch is merged into its line by its contents.
	struct ContentMark
	{
		enum class State
		{
			Pending, // not checked yet, or the check runs
			Known,
			Unknown, // the check could not answer: an object it needs is missing, or it failed
		} State = State::Pending;

		std::optional<Core::ContentVerdict> Verdict; // when Known
	};

	// Whether a line holds a copy of a commit: a commit carrying the same patch.
	struct TwinMark
	{
		enum ContentMark::State State = ContentMark::State::Pending;
		std::optional<Core::Oid> Twin; // when Known and the line holds one
	};

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

		// Asked like the shown branch, before the rows in view, and only while the window is on screen.
		TwinMark TwinIn(const Core::Oid& commit, const Core::Oid& lineTip);

		static constexpr float kSecondsBetweenSaves = 5.0f;

	private:
		struct TwinKey
		{
			Core::Oid Commit;
			Core::Oid LineTip;

			friend auto operator<=>(const TwinKey&, const TwinKey&) = default;
		};

		void Deliver(DeliveryInbox::Delivery delivery);

		// Read on worker threads: a job of a folder cleared since it was asked does nothing.
		bool IsSameFolder(std::uint64_t folder) const;

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
		std::optional<TwinKey> FirstTwinWaiting(const std::vector<TwinKey>& asked) const;
		bool IsTwinAsked(const TwinKey& key) const;

		void StartNext(const std::set<std::size_t>& inView, std::optional<std::size_t> shown, const std::vector<TwinKey>& twinsAsked);
		void StartRecheck();
		void StartCheck(std::size_t branch);
		void StartTwin(const TwinKey& key);

		void LandRecheck(const std::vector<RememberedVerdict>& held);
		void LandCheck(const ContentKey& key, const std::string& lineName, const Core::Result<Core::ContentVerdict>& verdict,
					   bool patchIdsGrew);
		void LandTwin(const TwinKey& key, const Core::Result<std::optional<Core::Oid>>& twin, bool patchIdsGrew);

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

		std::set<ContentKey> m_Unknown;         // could not answer: asked again in the next session
		std::set<ContentKey> m_RecheckWaiting;  // found merged against another line tip; does this one hold it?
		std::set<ContentKey> m_RecheckRunning;
		bool m_EveryCandidateAsked = false;     // of the followed read

		std::map<TwinKey, TwinMark> m_Twins;    // answered

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

		// WARNING: Bumped by Clear, which the folder events run before the port is pointed at the next folder, and read by
		// every job as it starts and every delivery as it lands: a check asked for the folder before must not land in the
		// next folder's verdicts or patch ids. A job stalled between that check and the port opening its repository is
		// refused by the port.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
