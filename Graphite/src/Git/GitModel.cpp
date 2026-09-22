#include "Git/GitModel.h"

#include "Core/Fs/FileSystem.h"
#include "Workspace/Workspace.h"

#include <utility>

namespace Graphite {

	GitModel::GitModel(GitPort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
	{
	}

	void GitModel::Deliver(Delivery delivery)
	{
		const std::lock_guard<std::mutex> lock{m_InboxMutex};
		m_Inbox.push_back(std::move(delivery));
	}

	void GitModel::SetRoot(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void GitModel::Clear()
	{
		// The generations move forward rather than resetting, so a query still in flight
		// for the folder that was just closed is dropped when it lands.
		m_StatusApplied = m_StatusRequested;
		m_BranchesApplied = m_BranchesRequested;
		m_LogApplied = m_LogRequested;
		m_WorkingTreeDiffApplied = m_WorkingTreeDiffRequested;

		m_Status.reset();
		m_WorkingDirectory.clear();
		m_Branches.reset();
		m_Log.reset();

		// Forgotten with the snapshot it belongs to: the next folder's first refresh must
		// not re-ask the previous folder's question, ref and filter and all.
		m_LastLogRequest.reset();
		m_WorkingTreeDiff.reset();
		m_WorkingTreeTexts.reset();
		m_CommitDiffs.clear();
		m_Texts.clear();
		m_Untracked.clear();

		m_StatusError.reset();
		m_BranchesError.reset();
		m_LogError.reset();
		m_WorkingTreeDiffError.reset();
		m_CommitDiffError.reset();
		m_TextsError.reset();

		// Moves forward rather than resetting, for the same reason the query generations do:
		// a write already in flight must be recognised as belonging to the folder that just
		// closed. m_WriteInFlight is deliberately left alone — that write is still running,
		// and claiming otherwise would let a second start beside it.
		++m_WriteGeneration;
		m_WriteQueue.clear();

		m_WriteError.reset();
		m_Committed.reset();
		m_Discarded.clear();

		m_Dirty = false;
		m_DirtyFor = 0.0f;
		m_DirtySince = 0.0f;
	}

	void GitModel::MarkDirty()
	{
		if (m_Root.empty())
		{
			return;
		}

		// The burst's start is only reset when it was not already dirty: a mark that arrives
		// mid-burst extends the quiet timer but must not push the cap out further, or a
		// sustained stream of events would never hit it.
		if (!m_Dirty)
		{
			m_DirtySince = 0.0f;
		}

		m_Dirty = true;
		m_DirtyFor = 0.0f;
	}

	void GitModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			// Opening a folder does not always follow a WorkspaceClosedEvent - dropping a
			// second folder onto the window replaces the workspace directly. Without Clear()
			// here, GetStatus()/GetBranches() would keep answering with the previous folder's
			// snapshot for the debounce window plus one query: a lie about the folder now
			// open. Clear() before SetRoot and MarkDirty, since Clear() sets m_Dirty = false
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
		if (m_Dirty)
		{
			m_DirtyFor += ts.GetSeconds();
			m_DirtySince += ts.GetSeconds();

			if (m_DirtyFor >= kDebounceSeconds || m_DirtySince >= kMaxDebounceSeconds)
			{
				m_Dirty = false;
				m_DirtyFor = 0.0f;
				m_DirtySince = 0.0f;

				RequestStatus();
				RequestBranches();

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
			}
		}

		std::vector<Delivery> ready;

		{
			const std::lock_guard<std::mutex> lock{m_InboxMutex};
			ready.swap(m_Inbox);
		}

		for (const Delivery& delivery : ready)
		{
			delivery();
		}
	}

	void GitModel::RequestStatus()
	{
		const std::uint64_t generation = ++m_StatusRequested;

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
				// A query that started earlier and finished later would otherwise put a
				// stale list back on screen.
				if (generation <= m_StatusApplied)
				{
					return;
				}

				m_StatusApplied = generation;

				if (!result.HasValue())
				{
					// The last good snapshot stays. A panel that blanks itself because one
					// refresh failed is worse than one showing data a second out of date.
					m_StatusError = result.GetError();
					return;
				}

				m_StatusError.reset();
				m_Status = std::make_shared<const Core::WorkingTreeStatus>(
					std::move(result).GetValue());

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
		const std::uint64_t generation = ++m_BranchesRequested;

		m_Submit([this, generation]() {
			auto result = m_Port.QueryBranches();

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_BranchesApplied)
				{
					return;
				}

				m_BranchesApplied = generation;

				if (!result.HasValue())
				{
					m_BranchesError = result.GetError();
					return;
				}

				m_BranchesError.reset();
				m_Branches = std::make_shared<const std::vector<Core::Branch>>(
					std::move(result).GetValue());
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

		const std::uint64_t generation = ++m_LogRequested;

		// Kept so a refresh can ask the same question again. The ref, the filter and the
		// page size are all the reader's, and a refresh that dropped them would replace the
		// list they are reading with an answer to a question nobody asked.
		m_LastLogRequest = request;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryLog(request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_LogApplied)
				{
					return;
				}

				m_LogApplied = generation;

				if (!result.HasValue())
				{
					m_LogError = result.GetError();
					return;
				}

				m_LogError.reset();
				m_Log = std::make_shared<const Core::LogPage>(std::move(result).GetValue());
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

		// The diff window's Render calls this every frame it is open, the same way the log
		// list asks RequestDiffFor about every expanded commit every frame. Without this
		// guard a diff that takes longer than a frame would resubmit a worker job many
		// times a second and never drain while the tab stayed open.
		if (m_WorkingTreeDiffRequested != m_WorkingTreeDiffApplied)
		{
			return;
		}

		const std::uint64_t generation = ++m_WorkingTreeDiffRequested;

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
						texts = std::move(read).GetValue();
					}
				}
			}

			Deliver([this, generation, result = std::move(result), texts = std::move(texts)]() mutable {
				if (generation <= m_WorkingTreeDiffApplied)
				{
					return;
				}

				m_WorkingTreeDiffApplied = generation;

				if (!result.HasValue())
				{
					m_WorkingTreeDiffError = result.GetError();
					return;
				}

				m_WorkingTreeDiffError.reset();
				m_WorkingTreeDiff =
					std::make_shared<const Core::Diff>(std::move(result).GetValue());

				// Replaced rather than kept: unlike a commit's text, which cannot change, this one
				// is different every time the file is touched. Cleared when there is none, so a
				// file too large to lex does not go on showing the last file's text.
				m_WorkingTreeTexts = texts.has_value()
					? std::make_shared<const FileTexts>(std::move(*texts))
					: nullptr;
			});
		});
	}

	void GitModel::Touch(CachedDiff& entry) const
	{
		entry.LastTouched = ++m_DiffClock;
	}

	void GitModel::EvictIfFull()
	{
		// A loop, not one shot: a single eviction only ever drops one entry, so once the
		// map is above the bound - which a run of requests that are all still in flight can
		// leave it at, since none of them were evictable at the time - one-in-one-out on
		// every later request would hold it there forever instead of bringing it back down.
		while (m_CommitDiffs.size() >= kCachedDiffs)
		{
			auto oldest = m_CommitDiffs.end();

			for (auto entry = m_CommitDiffs.begin(); entry != m_CommitDiffs.end(); ++entry)
			{
				// An entry still in flight is never evicted: its delivery would then arrive
				// for a key nobody holds and be dropped, and the reader would sit looking
				// at a row that never fills in.
				if (entry->second.Requested != entry->second.Applied)
				{
					continue;
				}

				if (oldest == m_CommitDiffs.end() || entry->second.LastTouched < oldest->second.LastTouched)
				{
					oldest = entry;
				}
			}

			// Nothing evictable this pass - every remaining entry is in flight. Stop rather
			// than spin: the map stays over the bound until one of them lands.
			if (oldest == m_CommitDiffs.end())
			{
				break;
			}

			m_CommitDiffs.erase(oldest);
		}
	}

	void GitModel::RequestDiffFor(const Core::Oid& commit)
	{
		// Mirrors MarkDirty and RequestLog: nothing on screen has any business asking about
		// a commit's diff for a folder that is not open.
		if (m_Root.empty())
		{
			return;
		}

		const auto existing = m_CommitDiffs.find(commit);

		if (existing != m_CommitDiffs.end())
		{
			const bool inFlight = existing->second.Requested != existing->second.Applied;

			// A failed entry is suppressed the same way a held or in-flight one is: the list
			// asks about every expanded commit every frame, and without this a permanent
			// failure would resubmit a worker job that many times a second.
			// ForgetFailedDiffFor is the only way back in.
			if (inFlight || existing->second.Diff != nullptr || existing->second.Failed)
			{
				Touch(existing->second);
				return;
			}
		}
		else
		{
			// Only when the key is absent: this is the only branch that grows the map. A
			// held, in-flight or failed entry above returns before reaching here, so
			// evicting there - where the key is already present and nothing will grow -
			// would drop an unrelated entry for no reason.
			EvictIfFull();
		}

		CachedDiff& entry = m_CommitDiffs[commit];
		Touch(entry);

		// From the model-wide counter, not a counter local to the entry - see the comment
		// on CachedDiff::Requested.
		const std::uint64_t generation = ++m_CommitDiffGeneration;
		entry.Requested = generation;

		Core::DiffRequest request;
		request.Target = Core::DiffTarget::CommitToParent;
		request.From = commit;

		m_Submit([this, commit, generation, request]() {
			auto result = m_Port.QueryDiff(request);

			Deliver([this, commit, generation, result = std::move(result)]() mutable {
				const auto entry = m_CommitDiffs.find(commit);

				// Evicted, or the workspace closed, while the query was running. Dropping
				// it is right: nothing on screen is waiting for it.
				if (entry == m_CommitDiffs.end() || generation <= entry->second.Applied)
				{
					return;
				}

				entry->second.Applied = generation;

				if (!result.HasValue())
				{
					// No last-good snapshot to keep, unlike the volatile diff: a commit
					// that failed has nothing older to fall back to. Marked rather than left
					// to imply itself from a null Diff, so RequestDiffFor can tell "still
					// waiting" from "already tried and lost" without a second lookup.
					m_CommitDiffError = result.GetError();
					entry->second.Failed = true;
					return;
				}

				m_CommitDiffError.reset();
				entry->second.Diff =
					std::make_shared<const Core::Diff>(std::move(result).GetValue());
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

				held->second.Listing = std::make_shared<const Core::WorkingTreeStatus>(
					std::move(listing).GetValue());
			});
		});
	}

	std::shared_ptr<const Core::WorkingTreeStatus> GitModel::GetUntrackedIn(
		const std::filesystem::path& directory) const
	{
		const auto held = m_Untracked.find(Core::ToUtf8(directory));

		return held == m_Untracked.end() ? nullptr : held->second.Listing;
	}

	void GitModel::ForgetFailedDiffFor(const Core::Oid& commit)
	{
		const auto entry = m_CommitDiffs.find(commit);

		// A successful entry stays: this is what makes the cache hold recently-seen commits
		// rather than only the currently-expanded ones. Only a failed entry is dropped, so
		// collapsing and re-expanding the row is the reader's way to retry it.
		if (entry == m_CommitDiffs.end() || !entry->second.Failed)
		{
			return;
		}

		m_CommitDiffs.erase(entry);

		// The error line must not outlive the row it described.
		m_CommitDiffError.reset();
	}

	std::shared_ptr<const Core::Diff> GitModel::GetDiffFor(const Core::Oid& commit) const
	{
		const auto entry = m_CommitDiffs.find(commit);

		if (entry == m_CommitDiffs.end())
		{
			return nullptr;
		}

		Touch(entry->second);
		return entry->second.Diff;
	}

	void GitModel::Touch(CachedTexts& entry) const
	{
		entry.LastTouched = ++m_TextsClock;
	}

	void GitModel::EvictTextsIfFull()
	{
		// Mirrors EvictIfFull exactly - see its comments for why this is a loop rather than
		// one shot, and why an in-flight entry is never a candidate.
		while (m_Texts.size() >= kCachedTexts)
		{
			auto oldest = m_Texts.end();

			for (auto entry = m_Texts.begin(); entry != m_Texts.end(); ++entry)
			{
				if (entry->second.Requested != entry->second.Applied)
				{
					continue;
				}

				if (oldest == m_Texts.end() || entry->second.LastTouched < oldest->second.LastTouched)
				{
					oldest = entry;
				}
			}

			if (oldest == m_Texts.end())
			{
				break;
			}

			m_Texts.erase(oldest);
		}
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

		const TextsKey key{commit, path};
		const auto existing = m_Texts.find(key);

		if (existing != m_Texts.end())
		{
			const bool inFlight = existing->second.Requested != existing->second.Applied;

			// Suppresses a failed entry the same way RequestDiffFor does - see its comment.
			if (inFlight || existing->second.Texts != nullptr || existing->second.Failed)
			{
				Touch(existing->second);
				return;
			}
		}
		else
		{
			// Only when the key is absent - see EvictIfFull's comment for why evicting on
			// any other branch would drop an unrelated entry for no reason.
			EvictTextsIfFull();
		}

		CachedTexts& entry = m_Texts[key];
		Touch(entry);

		// From the model-wide counter, not a counter local to the entry - see the comment
		// on CachedTexts::Requested.
		const std::uint64_t generation = ++m_TextsGeneration;
		entry.Requested = generation;

		m_Submit([this, commit, path, oldPath, newPath, generation]() {
			auto result = m_Port.ReadFileTexts(commit, oldPath, newPath);

			Deliver([this, commit, path, generation, result = std::move(result)]() mutable {
				const TextsKey key{commit, path};
				const auto entry = m_Texts.find(key);

				// Evicted, or the workspace closed, while the query was running. Dropping
				// it is right: nothing on screen is waiting for it.
				if (entry == m_Texts.end() || generation <= entry->second.Applied)
				{
					return;
				}

				entry->second.Applied = generation;

				if (!result.HasValue())
				{
					// No last-good snapshot to keep, unlike the volatile working-tree diff:
					// a file whose texts failed has nothing older to fall back to.
					m_TextsError = result.GetError();
					entry->second.Failed = true;
					return;
				}

				m_TextsError.reset();
				entry->second.Texts =
					std::make_shared<const FileTexts>(std::move(result).GetValue());
			});
		});
	}

	void GitModel::ForgetFailedTextsFor(const Core::Oid& commit, const std::filesystem::path& path)
	{
		const auto entry = m_Texts.find(TextsKey{commit, path});

		// Mirrors ForgetFailedDiffFor: a successful entry stays, only a failed one is
		// dropped, so asking again for the same file is the reader's way to retry it.
		if (entry == m_Texts.end() || !entry->second.Failed)
		{
			return;
		}

		m_Texts.erase(entry);

		// The error line must not outlive the entry it described.
		m_TextsError.reset();
	}

	std::shared_ptr<const FileTexts> GitModel::GetTextsFor(
		const Core::Oid& commit, const std::filesystem::path& path) const
	{
		const auto entry = m_Texts.find(TextsKey{commit, path});

		if (entry == m_Texts.end())
		{
			return nullptr;
		}

		Touch(entry->second);
		return entry->second.Texts;
	}
}
