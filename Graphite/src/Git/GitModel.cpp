#include "Git/GitModel.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Workspace/Workspace.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Graphite {

	GitModel::GitModel(GitPort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
	{
	}

	void GitModel::Deliver(Delivery delivery)
	{
		m_Inbox.Post(std::move(delivery));
	}

	void GitModel::SetRoot(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void GitModel::Clear()
	{
		// Each slot's generations move forward rather than resetting, so a query still in
		// flight for the folder that was just closed is dropped when it lands.
		m_Status.Clear();
		m_WorkingDirectory.clear();
		m_Branches.Clear();
		m_Stashes.Clear();
		m_Head.Clear();
		m_Log.Clear();

		// Forgotten with the snapshot it belongs to: the next folder's first refresh must
		// not re-ask the previous folder's question, ref and filter and all.
		m_LastLogRequest.reset();
		m_WorkingTreeDiff.Clear();
		m_WorkingTreeTexts.reset();
		m_WantedWorkingTreeDiff.reset();
		m_WorkingTreeDiffFor.reset();
		m_WorkingTreeDiffSettled.reset();
		m_WorkingTreeDiffStale = false;
		m_CommitDiffs.Clear();
		m_Texts.Clear();
		m_Untracked.clear();

		m_InitSettlesAfter.reset();
		m_CommitDiffError.reset();
		m_TextsError.reset();

		// Moves forward rather than resetting, for the same reason the query generations do:
		// a write already in flight must be recognised as belonging to the folder that just
		// closed. m_WriteInFlight is deliberately left alone — that write is still running,
		// and claiming otherwise would let a second start beside it.
		++m_WriteGeneration;
		m_WriteQueue.clear();

		m_WriteErrors.clear();
		m_InitError.reset();
		m_Committed.reset();
		m_Discarded.clear();
		m_CheckedOut.clear();
		m_CheckoutPlan.reset();
		m_DeleteCheck.reset();

		m_Debounce.Reset();
	}

	void GitModel::MarkDirty()
	{
		if (m_Root.empty())
		{
			return;
		}

		m_Debounce.Mark();
	}

	void GitModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			// Opening a folder does not always follow a WorkspaceClosedEvent - dropping a
			// second folder onto the window replaces the workspace directly. Without Clear()
			// here, GetStatus()/GetBranches() would keep answering with the previous folder's
			// snapshot for the debounce window plus one query: a lie about the folder now
			// open. Clear() before SetRoot and MarkDirty, since Clear() resets the debounce
			// and would otherwise undo the mark.
			Clear();
			SetRoot(event.Root);
			MarkDirty();
		});

		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			m_Root.clear();
			Clear();
		});

		events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent&) { MarkDirty(); });

		// The same response as a file change, and deliberately so: what is stale is the same
		// set of answers, and the debounce that keeps a save from querying five times keeps
		// a rebase from querying once per rewritten commit.
		events.Subscribe<RepositoryChangedEvent>(
			[this](const RepositoryChangedEvent&) { MarkDirty(); });
	}

	void GitModel::OnUpdate(Core::Timestep ts)
	{
		if (m_Debounce.Advance(ts))
		{
			RequestStatus();
			RequestBranches();
			RequestHead();
			RequestStashes();

			// A commit, an amend, a rebase or a fetch all change the history, and
			// nothing else re-asks for it: without this the list keeps showing what it
			// showed when the panel was opened. Only when something has already asked
			// for a log - with no list on screen there is nothing to keep fresh, and
			// walking the history for a panel the reader may never open is work spent
			// on nobody.
			if (m_LastLogRequest.has_value())
			{
				RequestLog(*m_LastLogRequest);
			}

			// The working-tree diff is only marked, not re-asked: the diff window asks every
			// frame it is visible, so a tab that is open sends the query on its next frame,
			// and a tab that has closed or gone behind another sends nothing.
			m_WorkingTreeDiffStale = true;
		}

		m_Inbox.Drain();
	}

	void GitModel::RequestStatus()
	{
		const std::uint64_t generation = m_Status.Begin();

		m_Submit([this, generation]() {
			auto result = m_Port.QueryStatus();

			// Fetched on the same worker job as the status itself, and delivered together, so
			// a panel that has a status snapshot always has the working directory that snapshot's
			// paths are relative to. Fetching this lazily on a separate path would leave a window
			// where a status exists but GetWorkingDirectory() still answers the previous folder's
			// - or, on the first refresh, the empty one - which is exactly the wrong-directory
			// join this exists to prevent.
			auto workingDirectory = m_Port.GetWorkingDirectory();

			Deliver([this, generation, result = std::move(result),
					 workingDirectory = std::move(workingDirectory)]() mutable {
				// A stale answer is dropped and a failure keeps the last good snapshot - see
				// GenerationSlot - and neither says anything new about the working tree.
				if (m_Status.Land(generation, std::move(result)) != SlotLanding::Applied)
				{
					return;
				}

				// What is untracked under an expanded directory changes as the working tree
				// does, unlike a commit's diff - a fresh status means every listing held so
				// far may already be stale, so all of them are dropped rather than kept
				// until their own next refresh.
				m_Untracked.clear();

				// A failure here is not folded into m_StatusError: the status itself is good,
				// and the previous working directory - correct for the same repository - is a
				// safer answer than discarding it over an unrelated, transient failure.
				if (workingDirectory.HasValue())
				{
					m_WorkingDirectory = std::move(workingDirectory).GetValue();
				}
			});
		});
	}

	void GitModel::RequestBranches()
	{
		const std::uint64_t generation = m_Branches.Begin();

		m_Submit([this, generation]() {
			auto result = m_Port.QueryBranches();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Branches.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestStashes()
	{
		const std::uint64_t generation = m_Stashes.Begin();

		m_Submit([this, generation]() {
			auto result = m_Port.QueryStashes();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Stashes.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestHead()
	{
		const std::uint64_t generation = m_Head.Begin();

		m_Submit([this, generation]() {
			auto result = m_Port.QueryHead();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Head.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestLog(Core::LogRequest request)
	{
		// Mirrors MarkDirty: nothing on screen has any business asking about a folder that
		// is not open. WorkspaceClosedEvent's handler resets GitView's log-requested latch
		// without knowing whether a new folder will open next, so without this a closed
		// workspace would still fire one query for the folder it no longer has.
		if (m_Root.empty())
		{
			return;
		}

		const std::uint64_t generation = m_Log.Begin();

		// Kept so a refresh can ask the same question again. The ref, the filter and the
		// page size are all the reader's, and a refresh that dropped them would replace the
		// list they are reading with an answer to a question nobody asked.
		m_LastLogRequest = request;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryLog(request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Log.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestWorkingTreeDiff(Core::DiffRequest request)
	{
		// Mirrors MarkDirty and RequestLog: nothing on screen has any business asking about
		// a working tree for a folder that is not open.
		if (m_Root.empty())
		{
			return;
		}

		m_WantedWorkingTreeDiff = std::move(request);
		PumpWorkingTreeDiff();
	}

	void GitModel::PumpWorkingTreeDiff()
	{
		if (!m_WantedWorkingTreeDiff.has_value())
		{
			return;
		}

		// The diff window's Render asks every frame it is open, the same way the log list
		// asks RequestDiffFor about every expanded commit every frame. Without this guard a
		// diff that takes longer than a frame would resubmit a worker job many times a
		// second and never drain while the tab stayed open. A request that changes while
		// one is in flight is not lost: the landing calls this again.
		if (m_WorkingTreeDiff.IsInFlight())
		{
			return;
		}

		// Already answered, and nothing has changed since the question was sent. Without
		// this every landing would be followed by another full query on the next frame, and
		// the frame loop would never idle while the tab is on screen.
		if (m_WorkingTreeDiffSettled == m_WantedWorkingTreeDiff && !m_WorkingTreeDiffStale)
		{
			return;
		}

		m_WorkingTreeDiffStale = false;

		const std::uint64_t generation = m_WorkingTreeDiff.Begin();
		Core::DiffRequest request = *m_WantedWorkingTreeDiff;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryDiff(request);

			// Read on the same job as the diff, and from the paths the diff itself reports, so
			// the whole-file text and the hunks drawn over it can never describe two different
			// moments of a file the user is still typing in - nor two different names, when the
			// working tree holds a rename.
			std::optional<FileTexts> texts;

			if (result.HasValue() && result.GetValue().Files.size() == 1)
			{
				const Core::FileDiff& file = result.GetValue().Files.front();

				// A binary file has no text to show and can be arbitrarily large; the pane says
				// so without needing any of its bytes.
				if (!file.IsBinary)
				{
					auto read = m_Port.ReadWorkingTreeTexts(request.Target, file.OldPath, file.NewPath);

					// A text that cannot be read - too large to lex, or gone between the diff and
					// the read - costs the pane its colouring and its whole-file view, and nothing
					// else. The diff itself still draws, so this is not the diff's failure to report.
					if (read.HasValue())
					{
						// Coloured here rather than in the delivery, which runs on the UI thread:
						// a C# side is a whole-file parse with no budget.
						texts = std::move(read).GetValue();
						texts->Tokens = ColourSides(*texts, file.OldPath, file.NewPath);
					}
				}
			}

			Deliver([this, generation, request = std::move(request), result = std::move(result),
					 texts = std::move(texts)]() mutable {
				const SlotLanding landing = m_WorkingTreeDiff.Land(generation, std::move(result));

				if (landing == SlotLanding::Stale)
				{
					return;
				}

				m_WorkingTreeDiffSettled = request;

				if (landing == SlotLanding::Failed)
				{
					PumpWorkingTreeDiff();
					return;
				}

				m_WorkingTreeDiffFor = std::move(request);

				// Replaced rather than kept: unlike a commit's text, which cannot change, this one
				// is different every time the file is touched. Cleared when there is none, so a
				// file too large to lex does not go on showing the last file's text.
				m_WorkingTreeTexts = texts.has_value()
										 ? Core::CreateRef<const FileTexts>(std::move(*texts))
										 : nullptr;

				PumpWorkingTreeDiff();
			});
		});
	}

	void GitModel::RequestDiffFor(const Core::Oid& commit)
	{
		// Mirrors MarkDirty and RequestLog: nothing on screen has any business asking about
		// a commit's diff for a folder that is not open.
		if (m_Root.empty())
		{
			return;
		}

		// Nothing when the commit is held, in flight or failed: the list asks about every
		// expanded commit every frame, and without this a permanent failure would resubmit a
		// worker job that many times a second. ForgetFailedDiffFor is the only way back in.
		const std::optional<std::uint64_t> generation = m_CommitDiffs.Request(commit);

		if (!generation.has_value())
		{
			return;
		}

		// A stash is a commit too, but one whose untracked files sit in a parent of their
		// own, which only the stash's own target reads. The list the panel shows is the one
		// asked about: a stash that is not in it was dropped, and is asked for as the commit
		// it still is.
		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Stashes.Get();
		const bool stash =
			stashes && std::ranges::find(*stashes, commit, &Core::StashEntry::Id) != stashes->end();

		Core::DiffRequest request;
		request.Target = stash ? Core::DiffTarget::StashToParent : Core::DiffTarget::CommitToParent;
		request.From = commit;

		m_Submit([this, commit, generation = *generation, request]() {
			auto result = m_Port.QueryDiff(request);

			Deliver([this, commit, generation, result = std::move(result)]() mutable {
				// Dropped by the cache when the commit was evicted, or the workspace closed, while
				// the query was running - nothing on screen is waiting for it then.
				if (!result.HasValue())
				{
					if (m_CommitDiffs.Fail(commit, generation))
					{
						m_CommitDiffError = result.GetError();
					}

					return;
				}

				auto diff = Core::CreateRef<const Core::Diff>(std::move(result).GetValue());

				if (m_CommitDiffs.Succeed(commit, generation, std::move(diff)))
				{
					m_CommitDiffError.reset();
				}
			});
		});
	}

	void GitModel::RequestUntrackedIn(std::filesystem::path directory)
	{
		if (m_Root.empty())
		{
			return;
		}

		const std::string key = Core::ToUtf8(directory);
		CachedUntracked& entry = m_Untracked[key];

		if (entry.Listing != nullptr || entry.Failed || entry.Requested > entry.Applied)
		{
			return;
		}

		++m_UntrackedGeneration;
		entry.Requested = m_UntrackedGeneration;

		const std::uint64_t generation = entry.Requested;

		m_Submit([this, directory = std::move(directory), key, generation]() {
			auto listing = m_Port.QueryUntrackedIn(directory);

			Deliver([this, key, generation, listing = std::move(listing)]() mutable {
				const auto held = m_Untracked.find(key);

				if (held == m_Untracked.end() || held->second.Applied >= generation)
				{
					return;
				}

				held->second.Applied = generation;

				if (!listing.HasValue())
				{
					held->second.Failed = true;
					return;
				}

				held->second.Listing = Core::CreateRef<const Core::WorkingTreeStatus>(
					std::move(listing).GetValue());
			});
		});
	}

	Core::Ref<const Core::WorkingTreeStatus> GitModel::GetUntrackedIn(
		const std::filesystem::path& directory) const
	{
		const auto held = m_Untracked.find(Core::ToUtf8(directory));

		return held == m_Untracked.end() ? nullptr : held->second.Listing;
	}

	void GitModel::ForgetFailedDiffFor(const Core::Oid& commit)
	{
		// A successful entry stays: this is what makes the cache hold recently-seen commits
		// rather than only the currently-expanded ones. Only a failed entry is dropped, so
		// collapsing and re-expanding the row is the reader's way to retry it.
		if (m_CommitDiffs.ForgetFailed(commit))
		{
			// The error line must not outlive the row it described.
			m_CommitDiffError.reset();
		}
	}

	bool GitModel::IsDiffFailed(const Core::Oid& commit) const
	{
		return m_CommitDiffs.IsFailed(commit);
	}

	Core::Ref<const Core::Diff> GitModel::GetDiffFor(const Core::Oid& commit) const
	{
		return m_CommitDiffs.Get(commit);
	}

	void GitModel::RequestTextsFor(
		const Core::Oid& commit,
		const std::filesystem::path& path,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		// Mirrors RequestDiffFor: nothing on screen has any business asking about a file's
		// text for a folder that is not open.
		if (m_Root.empty())
		{
			return;
		}

		// Suppresses a held, in-flight or failed entry the same way RequestDiffFor does - see
		// its comment.
		const std::optional<std::uint64_t> generation = m_Texts.Request(TextsKey{commit, path});

		if (!generation.has_value())
		{
			return;
		}

		m_Submit([this, commit, path, oldPath, newPath, generation = *generation]() {
			auto result = m_Port.ReadFileTexts(commit, oldPath, newPath);

			// Coloured here rather than in the delivery, which runs on the UI thread: a C# side is
			// a whole-file parse with no budget.
			if (result.HasValue())
			{
				FileTexts& texts = result.GetValue();
				texts.Tokens = ColourSides(texts, oldPath, newPath);
			}

			Deliver([this, commit, path, generation, result = std::move(result)]() mutable {
				const TextsKey key{commit, path};

				// Dropped by the cache when the file was evicted, or the workspace closed, while
				// the query was running - nothing on screen is waiting for it then.
				if (!result.HasValue())
				{
					if (m_Texts.Fail(key, generation))
					{
						m_TextsError = result.GetError();
					}

					return;
				}

				auto texts = Core::CreateRef<const FileTexts>(std::move(result).GetValue());

				if (m_Texts.Succeed(key, generation, std::move(texts)))
				{
					m_TextsError.reset();
				}
			});
		});
	}

	void GitModel::ForgetFailedTextsFor(const Core::Oid& commit, const std::filesystem::path& path)
	{
		// Mirrors ForgetFailedDiffFor: a successful entry stays, only a failed one is
		// dropped, so asking again for the same file is the reader's way to retry it.
		if (m_Texts.ForgetFailed(TextsKey{commit, path}))
		{
			// The error line must not outlive the entry it described.
			m_TextsError.reset();
		}
	}

	Core::Ref<const FileTexts> GitModel::GetTextsFor(
		const Core::Oid& commit, const std::filesystem::path& path) const
	{
		return m_Texts.Get(TextsKey{commit, path});
	}
}
