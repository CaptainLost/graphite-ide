#include "Git/GitModel.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Workspace/Workspace.h"

#include <set>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		bool ContainsStash(const std::vector<Core::StashEntry>& stashes, const Core::Oid& commit)
		{
			for (const Core::StashEntry& stash : stashes)
			{
				if (stash.Id == commit)
				{
					return true;
				}
			}

			return false;
		}

		Core::Result<BranchAuthors> ReadBranchAuthors(GitPort& port, const Core::OwnAuthorsRequest& request)
		{
			BranchAuthors authors;

			Core::Result<std::optional<std::string>> email = port.ReadUserEmail();

			if (!email.HasValue())
			{
				return email.GetError();
			}

			authors.UserEmail = std::move(email).GetValue();

			// WARNING: Without a base every commit a tip reaches would count as its own, naming
			// everyone in the history; each branch then counts its tip's author instead.
			if (request.Hidden.empty())
			{
				return authors;
			}

			Core::Result<std::vector<Core::TipAuthors>> answers = port.QueryOwnAuthors(request);

			if (!answers.HasValue())
			{
				return answers.GetError();
			}

			for (Core::TipAuthors& answer : std::move(answers).GetValue())
			{
				const Core::Oid tip = answer.Tip;
				authors.ByTip.emplace(tip, std::move(answer));
			}

			return authors;
		}
	}

	GitModel::GitModel(GitPort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(submit), m_SubmitWrite(std::move(submit))
	{
	}

	GitModel::GitModel(GitPort& port, JobSubmitter submitRead, JobSubmitter submitWrite)
		: m_Port(port), m_Submit(std::move(submitRead)), m_SubmitWrite(std::move(submitWrite))
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
		m_Status.Clear();
		m_WorkingDirectory.clear();
		m_Branches.Clear();
		m_BranchAuthors.Clear();
		m_LastAuthorsRequest.reset();
		m_AuthorsHidden.reset();
		m_UserEmail.Clear();
		m_Remotes.Clear();
		m_Stashes.Clear();
		m_Head.Clear();
		m_Log.Clear();

		m_LastLogRequest.reset();
		m_ShownLogRequest.reset();
		m_LogRefreshPending = false;
		m_CommitCount.Clear();
		m_MarkedLog.Clear();
		m_LastMarkedRequest.reset();
		m_MarkedLabel.clear();
		m_WorkingTreeDiff.Clear();
		m_WorkingTreeTexts.reset();
		m_WorkingTreeTextsFailure.reset();
		m_WantedWorkingTreeDiff.reset();
		m_WorkingTreeDiffFor.reset();
		m_WorkingTreeDiffSettled.reset();
		m_WorkingTreeDiffStale = false;
		m_CommitDiffs.Clear();
		m_Brought.Clear();
		m_Texts.Clear();
		m_Untracked.clear();

		m_InitSettlesAfter.reset();
		m_CommitDiffError.reset();
		m_TextsError.reset();

		// WARNING: m_WriteInFlight is left alone on purpose. A write still running belongs to
		// the folder that just closed, and claiming otherwise would let a second start beside it.
		++m_WriteGeneration;
		m_WriteQueue.clear();

		m_WriteErrors.clear();
		m_InitError.reset();
		m_Committed.reset();
		m_Discarded.clear();
		m_CheckedOut.clear();
		m_CheckoutPlan.reset();
		m_DeleteCheck.reset();

		m_Tickets.clear();
		m_Confirmed.reset();

		m_Debounce.Reset();
	}

	void GitModel::MarkDirty()
	{
		if (!HasOpenFolder())
		{
			return;
		}

		m_Debounce.Mark();
	}

	void GitModel::SubscribeTo(Core::EventBus& events)
	{
		// WARNING: Opening a folder does not always follow a WorkspaceClosedEvent, so Clear()
		// runs first here, and before MarkDirty because Clear() resets the debounce.
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
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

		events.Subscribe<RepositoryChangedEvent>(
			[this](const RepositoryChangedEvent&) { MarkDirty(); });
	}

	void GitModel::OnUpdate(Core::Timestep timestep)
	{
		// WARNING: Before the deliveries are drained, so a confirmation settled this frame keeps its full time.
		CountDownConfirmation(timestep);

		if (m_Debounce.Advance(timestep))
		{
			RequestStatus();
			RequestBranches();
			RequestHead();
			RequestStashes();

			// WARNING: A refresh while a log is being read waits for it and asks once it lands. After
			// Load all or Search all the walk has no limit, and a build writing into the workspace
			// refreshes about once a second: asking at once would queue a whole-history walk behind
			// itself, again and again, on the job queue every other feature shares.
			if (m_LastLogRequest.has_value())
			{
				if (m_Log.IsInFlight())
				{
					m_LogRefreshPending = true;
				}
				else
				{
					RequestLog(*m_LastLogRequest, m_LastMarkedRequest);
				}
			}

			m_WorkingTreeDiffStale = true;
		}

		m_Inbox.Drain();

		SettleTicketsWhoseStatusLanded();
	}

	void GitModel::RequestStatus()
	{
		RequestStatusOn(m_Submit);
	}

	void GitModel::ReadStatusAfterWrite()
	{
		RequestStatusOn(m_SubmitWrite);
		RequestHeadOn(m_SubmitWrite);
	}

	void GitModel::RequestStatusOn(const JobSubmitter& submit)
	{
		const std::uint64_t generation = m_Status.Begin();

		submit([this, generation]() {
			Core::Result<Core::WorkingTreeStatus> result = m_Port.QueryStatus();
			Core::Result<std::filesystem::path> workingDirectory = m_Port.GetWorkingDirectory();

			Deliver([this, generation, result = std::move(result),
					 workingDirectory = std::move(workingDirectory)]() mutable {
				if (m_Status.Land(generation, std::move(result)) != SlotLanding::Applied)
				{
					return;
				}

				KeepUntrackedListingsOf(*m_Status.Get());

				if (workingDirectory.HasValue())
				{
					m_WorkingDirectory = std::move(workingDirectory).GetValue();
				}
			});
		});
	}

	void GitModel::RequestBranches()
	{
		RequestUserEmail();
		RequestRemotes();

		const std::uint64_t generation = m_Branches.Begin();

		// WARNING: A listing equal to the snapshot held keeps that snapshot's pointer. Every reader follows the snapshot by
		// pointer, and a refresh comes with every save, so a new pointer for the same branches would have the line model,
		// the reviews and the lists all work out again what they show. Compared on the worker, so a listing of thousands
		// of branches is never walked on the UI thread.
		m_Submit([this, generation, held = m_Branches.Get()]() {
			Core::Result<std::vector<Core::Branch>> result = m_Port.QueryBranches();
			const bool sameAsHeld = held != nullptr && result.HasValue() && result.GetValue() == *held;

			Deliver([this, generation, held, sameAsHeld, result = std::move(result)]() mutable {
				const bool stillHeld = sameAsHeld && m_Branches.Get() == held;
				const SlotLanding landing = stillHeld ? m_Branches.LandSameAsHeld(generation) : m_Branches.Land(generation, std::move(result));

				if (landing == SlotLanding::Applied)
				{
					RequestBranchAuthorsWhenTipsMoved();
				}
			});
		});
	}

	void GitModel::RequestUserEmail()
	{
		const std::uint64_t generation = m_UserEmail.Begin();

		m_Submit([this, generation]() {
			Core::Result<std::optional<std::string>> result = m_Port.ReadUserEmail();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_UserEmail.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestRemotes()
	{
		const std::uint64_t generation = m_Remotes.Begin();

		m_Submit([this, generation]() {
			Core::Result<std::vector<Core::Remote>> result = m_Port.QueryRemotes();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Remotes.Land(generation, std::move(result));
			});
		});
	}

	const std::optional<std::string>& GitModel::GetUserEmail() const
	{
		static const std::optional<std::string> kUnread;
		const Core::Ref<const std::optional<std::string>> email = m_UserEmail.Get();

		return email ? *email : kUnread;
	}

	void GitModel::SetAuthorsHidden(std::vector<Core::Oid> baseTips)
	{
		if (m_AuthorsHidden == baseTips)
		{
			return;
		}

		m_AuthorsHidden = std::move(baseTips);

		if (m_Branches.Get())
		{
			RequestBranchAuthorsWhenTipsMoved();
		}
	}

	void GitModel::RequestBranchAuthorsWhenTipsMoved()
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Branches.Get();

		// WARNING: The line read says which tips are the bases; walking before it lands would count every commit as own.
		if (!m_AuthorsHidden.has_value())
		{
			return;
		}

		Core::OwnAuthorsRequest request = OwnAuthorsRequestFor(*branches, *m_AuthorsHidden);

		// WARNING: An answer without the user's e-mail is asked again by every listing, or a
		// user.email set afterwards would wait for some tip to move before the user is pinned first
		// in the authors list.
		const Core::Ref<const BranchAuthors> heldAuthors = m_BranchAuthors.Get();
		const bool userEmailUnknown = heldAuthors && !heldAuthors->UserEmail.has_value();

		if (m_LastAuthorsRequest == request && !userEmailUnknown)
		{
			return;
		}

		m_LastAuthorsRequest = request;

		const std::uint64_t generation = m_BranchAuthors.Begin();

		m_Submit([this, generation, request = std::move(request)]() {
			Core::Result<BranchAuthors> result = ReadBranchAuthors(m_Port, request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				const SlotLanding landing = m_BranchAuthors.Land(generation, std::move(result));

				// WARNING: A failed read is forgotten, or it would stand until some tip moves.
				if (landing == SlotLanding::Failed)
				{
					m_LastAuthorsRequest.reset();
				}
			});
		});
	}

	void GitModel::RequestStashes()
	{
		const std::uint64_t generation = m_Stashes.Begin();

		m_Submit([this, generation]() {
			Core::Result<std::vector<Core::StashEntry>> result = m_Port.QueryStashes();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Stashes.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestHead()
	{
		RequestHeadOn(m_Submit);
	}

	void GitModel::RequestHeadOn(const JobSubmitter& submit)
	{
		const std::uint64_t generation = m_Head.Begin();

		submit([this, generation]() {
			Core::Result<Core::HeadState> result = m_Port.QueryHead();

			// WARNING: The window bar names the folder beside HEAD, so the folder is read with HEAD: with the status
			// alone it would come seconds later in a large working tree, and the bar would wait for it that long.
			Core::Result<std::filesystem::path> workingDirectory = m_Port.GetWorkingDirectory();

			Deliver([this, generation, result = std::move(result), workingDirectory = std::move(workingDirectory)]() mutable {
				if (m_Head.Land(generation, std::move(result)) != SlotLanding::Stale && workingDirectory.HasValue())
				{
					m_WorkingDirectory = std::move(workingDirectory).GetValue();
				}
			});
		});
	}

	void GitModel::RequestLog(Core::LogRequest request, std::optional<MarkedCommitsRequest> marked)
	{
		// A new ask reads the repository as it is now, so a refresh held for the log in flight is
		// answered by it.
		m_LogRefreshPending = false;

		if (!HasOpenFolder())
		{
			return;
		}

		const std::uint64_t generation = m_Log.Begin();
		const std::uint64_t markedGeneration = m_MarkedLog.Begin();

		m_LastLogRequest = request;
		m_LastMarkedRequest = marked;

		// Copied before the request moves into the log's job, which goes first: on a single worker the
		// first page of a new ref would otherwise wait for a walk of its whole history.
		const std::optional<std::string> countedRef = request.Ref;
		const Core::FollowedParents countedParents =
			request.FirstParentOnly ? Core::FollowedParents::FirstOnly : Core::FollowedParents::All;
		const bool isStash = request.Stash.has_value();

		m_Submit([this, generation, markedGeneration, request = std::move(request), marked = std::move(marked)]() {
			Core::Result<Core::LogPage> result = m_Port.QueryLog(request);

			std::optional<Core::Result<Core::LogPage>> markedResult;

			if (marked.has_value())
			{
				markedResult = m_Port.QueryLog(marked->Query);
			}

			std::string label = marked.has_value() ? marked->Label : std::string{};
			const MarkedKind kind = marked.has_value() ? marked->Kind : MarkedKind::Own;

			Deliver([this, generation, markedGeneration, asked = request, result = std::move(result),
					 markedResult = std::move(markedResult), label = std::move(label), kind]() mutable {
				if (m_Log.Land(generation, std::move(result)) == SlotLanding::Applied)
				{
					m_ShownLogRequest = std::move(asked);
					LandMarkedCommits(markedGeneration, std::move(markedResult), std::move(label), kind);
				}

				// Whatever the landing, applied, failed or stale: each of them can be the last log in flight.
				AskHeldLogRefresh();
			});
		});

		if (!isStash)
		{
			RequestCommitCount(countedRef, countedParents);
		}
	}

	std::optional<GitModel::MarkedKind> GitModel::GetMarkedKind() const
	{
		if (!m_MarkedLog.Get())
		{
			return std::nullopt;
		}

		return m_MarkedKind;
	}

	void GitModel::LandMarkedCommits(std::uint64_t generation, std::optional<Core::Result<Core::LogPage>> result,
									 std::string label, MarkedKind kind)
	{
		if (!result.has_value())
		{
			m_MarkedLog.Clear();
			m_MarkedLabel.clear();
			return;
		}

		if (m_MarkedLog.Land(generation, std::move(*result)) == SlotLanding::Applied)
		{
			m_MarkedLabel = std::move(label);
			m_MarkedKind = kind;
		}
		else
		{
			m_MarkedLog.Clear();
			m_MarkedLabel.clear();
		}
	}

	void GitModel::AskHeldLogRefresh()
	{
		if (!m_LogRefreshPending || m_Log.IsInFlight())
		{
			return;
		}

		m_LogRefreshPending = false;
		RequestLog(*m_LastLogRequest, m_LastMarkedRequest);
	}

	void GitModel::RequestCommitCount(const std::optional<std::string>& ref, Core::FollowedParents parents)
	{
		const Core::Ref<const RefCommitCount> held = m_CommitCount.Get();
		const std::optional<CommitCount> known = held && held->Ref == ref && held->Parents == parents
													 ? std::optional<CommitCount>{held->Count}
													 : std::nullopt;

		const std::uint64_t generation = m_CommitCount.Begin();

		m_Submit([this, generation, ref, parents, known]() {
			Core::Result<CommitCount> result = m_Port.CountCommits(ref, parents, known);

			Deliver([this, generation, ref, parents, result = std::move(result)]() mutable {
				if (!result.HasValue())
				{
					m_CommitCount.Land(generation, Core::Result<RefCommitCount>{result.GetError()});
					return;
				}

				m_CommitCount.Land(generation, RefCommitCount{.Ref = ref, .Parents = parents, .Count = result.GetValue()});
			});
		});
	}

	std::optional<std::size_t> GitModel::GetCommitCountFor(const std::optional<std::string>& ref,
															 Core::FollowedParents parents) const
	{
		const Core::Ref<const RefCommitCount> held = m_CommitCount.Get();

		if (!held || held->Ref != ref || held->Parents != parents)
		{
			return std::nullopt;
		}

		return held->Count.Count;
	}

	void GitModel::RequestWorkingTreeDiff(Core::DiffRequest request)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		m_WantedWorkingTreeDiff = std::move(request);
		PumpWorkingTreeDiff();
	}

	// WARNING: The diff window asks every frame it is open. Without the in-flight and settled
	// guards below, a slow or permanently failing diff would resubmit a worker job every frame.
	void GitModel::PumpWorkingTreeDiff()
	{
		if (!m_WantedWorkingTreeDiff.has_value())
		{
			return;
		}

		if (m_WorkingTreeDiff.IsInFlight())
		{
			return;
		}

		if (m_WorkingTreeDiffSettled == m_WantedWorkingTreeDiff && !m_WorkingTreeDiffStale)
		{
			return;
		}

		m_WorkingTreeDiffStale = false;

		const std::uint64_t generation = m_WorkingTreeDiff.Begin();
		Core::DiffRequest request = *m_WantedWorkingTreeDiff;

		m_Submit([this, generation, request = std::move(request)]() {
			Core::Result<Core::Diff> result = m_Port.QueryDiff(request);

			std::optional<FileTexts> texts;
			std::optional<Core::Error> textsFailure;

			if (result.HasValue() && result.GetValue().Files.size() == 1)
			{
				const Core::FileDiff& file = result.GetValue().Files.front();

				if (!file.IsBinary)
				{
					Core::Result<FileTexts> read =
						m_Port.ReadWorkingTreeTexts(request.Target, file.OldPath, file.NewPath);

					if (read.HasValue())
					{
						texts = std::move(read).GetValue();
						texts->Tokens = ColourSides(*texts, file.OldPath, file.NewPath);
					}
					else
					{
						textsFailure = read.GetError();
					}
				}
			}

			Deliver([this, generation, request = std::move(request), result = std::move(result),
					 texts = std::move(texts), textsFailure = std::move(textsFailure)]() mutable {
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

				m_WorkingTreeTexts = texts.has_value()
										 ? Core::CreateRef<const FileTexts>(std::move(*texts))
										 : nullptr;
				m_WorkingTreeTextsFailure = std::move(textsFailure);

				PumpWorkingTreeDiff();
			});
		});
	}

	std::optional<Core::Error> GitModel::GetWorkingTreeDiffFailure() const
	{
		const bool wantedAnswerSettled =
			m_WantedWorkingTreeDiff.has_value() && m_WorkingTreeDiffSettled == m_WantedWorkingTreeDiff;

		if (!wantedAnswerSettled || AnswersWantedWorkingTreeDiff())
		{
			return std::nullopt;
		}

		return m_WorkingTreeDiff.GetError();
	}

	void GitModel::RetryWorkingTreeDiff()
	{
		m_WorkingTreeDiffSettled.reset();
		PumpWorkingTreeDiff();
	}

	void GitModel::RequestDiffFor(const Core::Oid& commit)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::optional<std::uint64_t> generation = m_CommitDiffs.Request(commit);

		if (!generation.has_value())
		{
			return;
		}

		const Core::Ref<const std::vector<Core::StashEntry>> stashes = m_Stashes.Get();
		const bool isStash = stashes != nullptr && ContainsStash(*stashes, commit);

		Core::DiffRequest request;
		request.Target = isStash ? Core::DiffTarget::StashToParent : Core::DiffTarget::CommitToParent;
		request.From = commit;

		m_Submit([this, commit, generation = *generation, request]() {
			Core::Result<Core::Diff> result = m_Port.QueryDiff(request);

			Deliver([this, commit, generation, result = std::move(result)]() mutable {
				if (!result.HasValue())
				{
					if (m_CommitDiffs.Fail(commit, generation, result.GetError()))
					{
						m_CommitDiffError = result.GetError();
					}

					return;
				}

				Core::Ref<const Core::Diff> diff =
					Core::CreateRef<const Core::Diff>(std::move(result).GetValue());

				if (m_CommitDiffs.Succeed(commit, generation, std::move(diff)))
				{
					m_CommitDiffError.reset();
				}
			});
		});
	}

	void GitModel::RequestBrought(const Core::Oid& merge, const Core::Oid& firstParent, const Core::Oid& secondParent)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::optional<std::uint64_t> generation = m_Brought.Request(merge);

		if (!generation.has_value())
		{
			return;
		}

		Core::LogRequest request;
		request.From = secondParent;
		request.Exclude = firstParent;
		request.FirstParentOnly = true;
		request.MaxCount = Core::kLogPageSize;

		m_Submit([this, merge, generation = *generation, request]() {
			Core::Result<Core::LogPage> result = m_Port.QueryLog(request);

			Deliver([this, merge, generation, result = std::move(result)]() mutable {
				if (!result.HasValue())
				{
					m_Brought.Fail(merge, generation, result.GetError());
					return;
				}

				m_Brought.Succeed(merge, generation, Core::CreateRef<const Core::LogPage>(std::move(result).GetValue()));
			});
		});
	}

	void GitModel::RequestUntrackedIn(std::filesystem::path directory)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::string directoryKey = Core::ToUtf8(directory);
		CachedUntracked& entry = m_Untracked[directoryKey];

		const bool inFlight = entry.Requested > entry.Applied;
		const bool answered = entry.Listing != nullptr || entry.Failure.has_value();

		if (inFlight || (answered && !entry.Stale))
		{
			return;
		}

		entry.Stale = false;

		++m_UntrackedGeneration;
		entry.Requested = m_UntrackedGeneration;

		const std::uint64_t generation = entry.Requested;

		m_Submit([this, directory = std::move(directory), directoryKey, generation]() {
			Core::Result<Core::WorkingTreeStatus> listing = m_Port.QueryUntrackedIn(directory);

			Deliver([this, directoryKey, generation, listing = std::move(listing)]() mutable {
				CachedUntracked* held = FindUntracked(directoryKey);

				if (held == nullptr || held->Applied >= generation)
				{
					return;
				}

				held->Applied = generation;

				if (!listing.HasValue())
				{
					held->Failure = listing.GetError();
					held->Listing.reset();
					return;
				}

				held->Failure.reset();
				held->Listing = Core::CreateRef<const Core::WorkingTreeStatus>(
					std::move(listing).GetValue());
			});
		});
	}

	void GitModel::KeepUntrackedListingsOf(const Core::WorkingTreeStatus& status)
	{
		std::set<std::string> listedDirectories;

		for (const Core::FileStatus& entry : status.Entries)
		{
			if (entry.IsDirectory)
			{
				listedDirectories.insert(Core::ToUtf8(entry.Path));
			}
		}

		UntrackedByDirectory kept;

		for (auto& [directoryKey, cached] : m_Untracked)
		{
			if (!listedDirectories.contains(directoryKey))
			{
				continue;
			}

			cached.Stale = true;
			kept.emplace(directoryKey, std::move(cached));
		}

		m_Untracked = std::move(kept);
	}

	GitModel::CachedUntracked* GitModel::FindUntracked(const std::string& directoryKey)
	{
		if (!m_Untracked.contains(directoryKey))
		{
			return nullptr;
		}

		return &m_Untracked.at(directoryKey);
	}

	const GitModel::CachedUntracked* GitModel::FindUntracked(const std::string& directoryKey) const
	{
		if (!m_Untracked.contains(directoryKey))
		{
			return nullptr;
		}

		return &m_Untracked.at(directoryKey);
	}

	Core::Ref<const Core::WorkingTreeStatus> GitModel::GetUntrackedIn(
		const std::filesystem::path& directory) const
	{
		const CachedUntracked* held = FindUntracked(Core::ToUtf8(directory));

		return held == nullptr ? nullptr : held->Listing;
	}

	bool GitModel::IsUntrackedListingCurrent(const std::filesystem::path& directory) const
	{
		const CachedUntracked* held = FindUntracked(Core::ToUtf8(directory));

		if (held == nullptr || held->Listing == nullptr)
		{
			return false;
		}

		const bool beingReadAgain = held->Requested > held->Applied;

		return !held->Stale && !beingReadAgain;
	}

	std::optional<Core::Error> GitModel::GetUntrackedFailureIn(const std::filesystem::path& directory) const
	{
		const CachedUntracked* held = FindUntracked(Core::ToUtf8(directory));

		return held == nullptr ? std::nullopt : held->Failure;
	}

	void GitModel::ForgetFailedDiffFor(const Core::Oid& commit)
	{
		if (m_CommitDiffs.ForgetFailed(commit))
		{
			m_CommitDiffError.reset();
		}
	}

	bool GitModel::IsDiffFailed(const Core::Oid& commit) const
	{
		return m_CommitDiffs.IsFailed(commit);
	}

	std::optional<Core::Error> GitModel::GetDiffFailure(const Core::Oid& commit) const
	{
		return m_CommitDiffs.FailureOf(commit);
	}

	Core::Ref<const Core::Diff> GitModel::GetDiffFor(const Core::Oid& commit) const
	{
		return m_CommitDiffs.Get(commit);
	}

	void GitModel::RequestTextsFor(
		const Core::Oid& commit,
		const std::filesystem::path& key,
		const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::optional<std::uint64_t> generation = m_Texts.Request(TextsKey{commit, key});

		if (!generation.has_value())
		{
			return;
		}

		m_Submit([this, commit, key, oldPath, newPath, generation = *generation]() {
			Core::Result<FileTexts> result = m_Port.ReadFileTexts(commit, oldPath, newPath);

			if (result.HasValue())
			{
				FileTexts& texts = result.GetValue();
				texts.Tokens = ColourSides(texts, oldPath, newPath);
			}

			Deliver([this, commit, key, generation, result = std::move(result)]() mutable {
				const TextsKey textsKey{commit, key};

				if (!result.HasValue())
				{
					if (m_Texts.Fail(textsKey, generation, result.GetError()))
					{
						m_TextsError = result.GetError();
					}

					return;
				}

				Core::Ref<const FileTexts> texts =
					Core::CreateRef<const FileTexts>(std::move(result).GetValue());

				if (m_Texts.Succeed(textsKey, generation, std::move(texts)))
				{
					m_TextsError.reset();
				}
			});
		});
	}

	void GitModel::ForgetFailedTextsFor(const Core::Oid& commit, const std::filesystem::path& key)
	{
		if (m_Texts.ForgetFailed(TextsKey{commit, key}))
		{
			m_TextsError.reset();
		}
	}

	Core::Ref<const FileTexts> GitModel::GetTextsFor(
		const Core::Oid& commit, const std::filesystem::path& key) const
	{
		return m_Texts.Get(TextsKey{commit, key});
	}

	bool GitModel::IsTextsFailed(const Core::Oid& commit, const std::filesystem::path& key) const
	{
		return m_Texts.IsFailed(TextsKey{commit, key});
	}

	std::optional<Core::Error> GitModel::GetTextsFailure(const Core::Oid& commit, const std::filesystem::path& key) const
	{
		return m_Texts.FailureOf(TextsKey{commit, key});
	}
}
