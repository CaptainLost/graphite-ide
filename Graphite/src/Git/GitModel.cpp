#include "Git/GitModel.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Workspace/Workspace.h"

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
	}

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
		m_Status.Clear();
		m_WorkingDirectory.clear();
		m_Branches.Clear();
		m_Stashes.Clear();
		m_Head.Clear();
		m_Log.Clear();

		m_LastLogRequest.reset();
		m_OwnLog.Clear();
		m_LastOwnRequest.reset();
		m_OwnLabel.clear();
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
		if (m_Debounce.Advance(timestep))
		{
			RequestStatus();
			RequestBranches();
			RequestHead();
			RequestStashes();

			if (m_LastLogRequest.has_value())
			{
				RequestLog(*m_LastLogRequest, m_LastOwnRequest);
			}

			m_WorkingTreeDiffStale = true;
		}

		m_Inbox.Drain();
	}

	void GitModel::RequestStatus()
	{
		const std::uint64_t generation = m_Status.Begin();

		m_Submit([this, generation]() {
			Core::Result<Core::WorkingTreeStatus> result = m_Port.QueryStatus();
			Core::Result<std::filesystem::path> workingDirectory = m_Port.GetWorkingDirectory();

			Deliver([this, generation, result = std::move(result),
					 workingDirectory = std::move(workingDirectory)]() mutable {
				if (m_Status.Land(generation, std::move(result)) != SlotLanding::Applied)
				{
					return;
				}

				m_Untracked.clear();

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
			Core::Result<std::vector<Core::Branch>> result = m_Port.QueryBranches();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Branches.Land(generation, std::move(result));
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
		const std::uint64_t generation = m_Head.Begin();

		m_Submit([this, generation]() {
			Core::Result<Core::HeadState> result = m_Port.QueryHead();

			Deliver([this, generation, result = std::move(result)]() mutable {
				m_Head.Land(generation, std::move(result));
			});
		});
	}

	void GitModel::RequestLog(Core::LogRequest request, std::optional<OwnCommitsRequest> ownCommits)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::uint64_t generation = m_Log.Begin();
		const std::uint64_t ownGeneration = m_OwnLog.Begin();

		m_LastLogRequest = request;
		m_LastOwnRequest = ownCommits;

		m_Submit([this, generation, ownGeneration, request = std::move(request),
				  ownCommits = std::move(ownCommits)]() {
			Core::Result<Core::LogPage> result = m_Port.QueryLog(request);

			std::optional<Core::Result<Core::LogPage>> ownResult;

			if (ownCommits.has_value())
			{
				ownResult = m_Port.QueryLog(ownCommits->Query);
			}

			std::string label = ownCommits.has_value() ? ownCommits->Label : std::string{};

			Deliver([this, generation, ownGeneration, result = std::move(result),
					 ownResult = std::move(ownResult), label = std::move(label)]() mutable {
				if (m_Log.Land(generation, std::move(result)) != SlotLanding::Applied)
				{
					return;
				}

				if (!ownResult.has_value())
				{
					m_OwnLog.Clear();
					m_OwnLabel.clear();
					return;
				}

				if (m_OwnLog.Land(ownGeneration, std::move(*ownResult)) == SlotLanding::Applied)
				{
					m_OwnLabel = std::move(label);
				}
				else
				{
					m_OwnLog.Clear();
					m_OwnLabel.clear();
				}
			});
		});
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

				m_WorkingTreeTexts = texts.has_value()
										 ? Core::CreateRef<const FileTexts>(std::move(*texts))
										 : nullptr;

				PumpWorkingTreeDiff();
			});
		});
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
					if (m_CommitDiffs.Fail(commit, generation))
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

	void GitModel::RequestUntrackedIn(std::filesystem::path directory)
	{
		if (!HasOpenFolder())
		{
			return;
		}

		const std::string directoryKey = Core::ToUtf8(directory);
		CachedUntracked& entry = m_Untracked[directoryKey];

		if (entry.Listing != nullptr || entry.Failed || entry.Requested > entry.Applied)
		{
			return;
		}

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
					held->Failed = true;
					return;
				}

				held->Listing = Core::CreateRef<const Core::WorkingTreeStatus>(
					std::move(listing).GetValue());
			});
		});
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
					if (m_Texts.Fail(textsKey, generation))
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
}
