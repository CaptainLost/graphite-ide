#include "Review/ReviewModel.h"

#include "Git/History/CommitLabels.h"
#include "Workspace/Workspace.h"

#include <utility>

namespace Graphite {

	ReviewModel::ReviewModel(ReviewPort& port, Core::JobSubmitter submit, WorktreeWatchFactory watch)
		: m_Port(port), m_Submit(std::move(submit)), m_Watch(std::move(watch))
	{
	}

	void ReviewModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });
	}

	void ReviewModel::OnUpdate(Core::Timestep ts)
	{
		for (auto& [key, uncommitted] : m_Uncommitted)
		{
			const bool worktreeChanged = uncommitted.Watch != nullptr && uncommitted.Watch->TakeChanged();

			if (worktreeChanged)
			{
				uncommitted.Debounce.Mark();
			}

			if (uncommitted.Debounce.Advance(ts))
			{
				uncommitted.Stale = true;
			}
		}

		m_Inbox.Drain();
	}

	void ReviewModel::Clear()
	{
		m_Ranges.clear();
		m_Uncommitted.clear();
		m_CommittedDiffs.Clear();
		m_CommittedTexts.Clear();
		m_CommittedDiffError.reset();
	}

	ReviewModel::RangeState* ReviewModel::FindRange(const ReviewKey& key, std::uint64_t incarnation)
	{
		for (auto& [heldKey, range] : m_Ranges)
		{
			if (heldKey == key && range.Incarnation == incarnation)
			{
				return &range;
			}
		}

		return nullptr;
	}

	const ReviewModel::RangeState* ReviewModel::FindRange(const ReviewKey& key) const
	{
		for (const auto& [heldKey, range] : m_Ranges)
		{
			if (heldKey == key)
			{
				return &range;
			}
		}

		return nullptr;
	}

	void ReviewModel::RequestRange(const ReviewKey& key)
	{
		RangeState& range = m_Ranges[key];

		if (range.Incarnation == 0)
		{
			range.Incarnation = ++m_NextIncarnation;
		}

		const bool answeredAndFresh = range.Asked && !range.Stale;

		if (range.Slot.IsInFlight() || answeredAndFresh)
		{
			return;
		}

		range.Asked = true;
		range.Stale = false;

		const std::uint64_t generation = range.Slot.Begin();
		const std::uint64_t incarnation = range.Incarnation;

		m_Submit([this, key, generation, incarnation]() {
			Core::Result<ReviewRange> answer = m_Port.ResolveRange(key);

			m_Inbox.Post([this, key, generation, incarnation, answer = std::move(answer)]() mutable {
				LandRange(key, incarnation, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandRange(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
								Core::Result<ReviewRange> answer)
	{
		RangeState* range = FindRange(key, incarnation);

		if (range == nullptr)
		{
			return;
		}

		range->Slot.Land(generation, std::move(answer));
	}

	Core::Ref<const ReviewRange> ReviewModel::GetRange(const ReviewKey& key) const
	{
		const RangeState* range = FindRange(key);
		return range == nullptr ? nullptr : range->Slot.Get();
	}

	const Core::Error* ReviewModel::GetRangeError(const ReviewKey& key) const
	{
		const RangeState* range = FindRange(key);

		if (range == nullptr || !range->Slot.GetError().has_value())
		{
			return nullptr;
		}

		return &*range->Slot.GetError();
	}

	bool ReviewModel::IsRangeLoading(const ReviewKey& key) const
	{
		const RangeState* range = FindRange(key);
		return range != nullptr && range->Slot.IsInFlight();
	}

	void ReviewModel::InvalidateRanges()
	{
		for (auto& [key, range] : m_Ranges)
		{
			range.Stale = true;
		}

		for (auto& [key, uncommitted] : m_Uncommitted)
		{
			uncommitted.Stale = true;
		}
	}

	void ReviewModel::Retry(const ReviewKey& key)
	{
		for (auto& [heldKey, range] : m_Ranges)
		{
			if (heldKey != key)
			{
				continue;
			}

			range.Stale = true;

			if (const Core::Ref<const ReviewRange> held = range.Slot.Get(); held != nullptr)
			{
				const bool forgotDiff = m_CommittedDiffs.ForgetFailed(held->Ends);

				if (forgotDiff)
				{
					m_CommittedDiffError.reset();
				}
			}
		}

		UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted != nullptr)
		{
			uncommitted->Stale = true;
		}
	}

	void ReviewModel::Forget(const ReviewKey& key)
	{
		m_Ranges.erase(key);
		m_Uncommitted.erase(key);
	}

	void ReviewModel::RequestCommittedDiff(const RangeEnds& ends)
	{
		const std::optional<std::uint64_t> generation = m_CommittedDiffs.Request(ends);

		if (!generation.has_value())
		{
			return;
		}

		m_Submit([this, ends, generation = *generation]() {
			Core::Result<Core::Diff> answer = m_Port.QueryRangeDiff(ends);

			m_Inbox.Post([this, ends, generation, answer = std::move(answer)]() mutable {
				LandCommittedDiff(ends, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandCommittedDiff(const RangeEnds& ends, std::uint64_t generation,
										Core::Result<Core::Diff> answer)
	{
		if (!answer.HasValue())
		{
			if (m_CommittedDiffs.Fail(ends, generation))
			{
				m_CommittedDiffError = answer.GetError();
			}

			return;
		}

		Core::Ref<const Core::Diff> diff = Core::CreateRef<const Core::Diff>(std::move(answer).GetValue());

		if (m_CommittedDiffs.Succeed(ends, generation, std::move(diff)))
		{
			m_CommittedDiffError.reset();
		}
	}

	Core::Ref<const Core::Diff> ReviewModel::GetCommittedDiff(const RangeEnds& ends) const
	{
		return m_CommittedDiffs.Get(ends);
	}

	bool ReviewModel::IsCommittedDiffFailed(const RangeEnds& ends) const
	{
		return m_CommittedDiffs.IsFailed(ends);
	}

	void ReviewModel::RequestCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file)
	{
		if (file.IsBinary)
		{
			return;
		}

		const TextsKey key{.Ends = ends, .Path = ShownPath(file)};
		const std::optional<std::uint64_t> generation = m_CommittedTexts.Request(key);

		if (!generation.has_value())
		{
			return;
		}

		m_Submit([this, key, oldPath = file.OldPath, newPath = file.NewPath, generation = *generation]() {
			Core::Result<FileTexts> answer = m_Port.ReadRangeTexts(key.Ends, oldPath, newPath);

			// Coloured here, on the worker: a C# side is a whole-file parse with no budget.
			if (answer.HasValue())
			{
				FileTexts& texts = answer.GetValue();
				texts.Tokens = ColourSides(texts, oldPath, newPath);
			}

			m_Inbox.Post([this, key, generation, answer = std::move(answer)]() mutable {
				LandCommittedTexts(key, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandCommittedTexts(const TextsKey& key, std::uint64_t generation,
										 Core::Result<FileTexts> answer)
	{
		// A text that cannot be read costs the pane its colouring and its whole-file view, and the
		// diff still draws, so the failure is held to stop a retry per frame and not reported.
		if (!answer.HasValue())
		{
			m_CommittedTexts.Fail(key, generation);
			return;
		}

		Core::Ref<const FileTexts> texts = Core::CreateRef<const FileTexts>(std::move(answer).GetValue());
		m_CommittedTexts.Succeed(key, generation, std::move(texts));
	}

	Core::Ref<const FileTexts> ReviewModel::GetCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file) const
	{
		return m_CommittedTexts.Get(TextsKey{.Ends = ends, .Path = ShownPath(file)});
	}

	ReviewModel::UncommittedState* ReviewModel::FindUncommitted(const ReviewKey& key)
	{
		for (auto& [heldKey, uncommitted] : m_Uncommitted)
		{
			if (heldKey == key)
			{
				return &uncommitted;
			}
		}

		return nullptr;
	}

	const ReviewModel::UncommittedState* ReviewModel::FindUncommitted(const ReviewKey& key) const
	{
		for (const auto& [heldKey, uncommitted] : m_Uncommitted)
		{
			if (heldKey == key)
			{
				return &uncommitted;
			}
		}

		return nullptr;
	}

	ReviewModel::UncommittedState* ReviewModel::FindUncommittedOf(const ReviewKey& key, std::uint64_t incarnation)
	{
		UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted == nullptr || uncommitted->Incarnation != incarnation)
		{
			return nullptr;
		}

		return uncommitted;
	}

	void ReviewModel::RequestUncommitted(const ReviewKey& key, const std::filesystem::path& worktree)
	{
		UncommittedState& uncommitted = m_Uncommitted[key];

		const bool watchingAnotherWorktree = uncommitted.Incarnation != 0 && uncommitted.Worktree != worktree;

		if (watchingAnotherWorktree)
		{
			uncommitted = UncommittedState{};
		}

		if (uncommitted.Incarnation == 0)
		{
			uncommitted.Incarnation = ++m_NextIncarnation;
			uncommitted.Worktree = worktree;
			uncommitted.Watch = m_Watch(worktree);
		}

		const bool answeredAndFresh = uncommitted.Asked && !uncommitted.Stale;

		if (uncommitted.Diff.IsInFlight() || answeredAndFresh)
		{
			return;
		}

		uncommitted.Asked = true;
		uncommitted.Stale = false;

		const std::uint64_t generation = uncommitted.Diff.Begin();
		const std::uint64_t incarnation = uncommitted.Incarnation;

		m_Submit([this, key, worktree, generation, incarnation]() {
			Core::Result<Core::Diff> answer = m_Port.QueryWorktreeDiff(worktree);

			m_Inbox.Post([this, key, generation, incarnation, answer = std::move(answer)]() mutable {
				LandUncommitted(key, incarnation, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandUncommitted(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
									  Core::Result<Core::Diff> answer)
	{
		UncommittedState* uncommitted = FindUncommittedOf(key, incarnation);

		if (uncommitted == nullptr)
		{
			return;
		}

		uncommitted->Diff.Land(generation, std::move(answer));
	}

	Core::Ref<const Core::Diff> ReviewModel::GetUncommitted(const ReviewKey& key) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);
		return uncommitted == nullptr ? nullptr : uncommitted->Diff.Get();
	}

	const Core::Error* ReviewModel::GetUncommittedError(const ReviewKey& key) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted == nullptr || !uncommitted->Diff.GetError().has_value())
		{
			return nullptr;
		}

		return &*uncommitted->Diff.GetError();
	}

	bool ReviewModel::IsUncommittedLoading(const ReviewKey& key) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);
		return uncommitted != nullptr && uncommitted->Diff.IsInFlight();
	}

	void ReviewModel::RequestUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file)
	{
		UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted == nullptr || file.IsBinary || uncommitted->Diff.Get() == nullptr)
		{
			return;
		}

		const std::filesystem::path& shownPath = ShownPath(file);
		const std::uint64_t diffAnswer = uncommitted->Diff.GetApplied();

		const bool askedForThisDiff = uncommitted->TextsRequested != 0 && uncommitted->TextsPath == shownPath &&
									  uncommitted->TextsForDiff == diffAnswer;
		const bool textsInFlight = uncommitted->TextsRequested != uncommitted->TextsApplied;

		if (askedForThisDiff || textsInFlight)
		{
			return;
		}

		uncommitted->TextsPath = shownPath;
		uncommitted->TextsForDiff = diffAnswer;
		uncommitted->TextsRequested = ++m_NextTextsGeneration;

		const std::uint64_t generation = uncommitted->TextsRequested;
		const std::uint64_t incarnation = uncommitted->Incarnation;

		m_Submit([this, key, incarnation, generation, worktree = uncommitted->Worktree, oldPath = file.OldPath,
				  newPath = file.NewPath]() {
			Core::Result<FileTexts> answer = m_Port.ReadWorktreeTexts(worktree, oldPath, newPath);

			// Coloured here, on the worker: a C# side is a whole-file parse with no budget.
			if (answer.HasValue())
			{
				FileTexts& texts = answer.GetValue();
				texts.Tokens = ColourSides(texts, oldPath, newPath);
			}

			m_Inbox.Post([this, key, incarnation, generation, answer = std::move(answer)]() mutable {
				LandUncommittedTexts(key, incarnation, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandUncommittedTexts(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
										   Core::Result<FileTexts> answer)
	{
		UncommittedState* uncommitted = FindUncommittedOf(key, incarnation);

		if (uncommitted == nullptr || generation != uncommitted->TextsRequested)
		{
			return;
		}

		uncommitted->TextsApplied = generation;
		uncommitted->Texts = answer.HasValue() ? Core::CreateRef<const FileTexts>(std::move(answer).GetValue()) : nullptr;
	}

	Core::Ref<const FileTexts> ReviewModel::GetUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted == nullptr)
		{
			return nullptr;
		}

		const bool forThisFile = uncommitted->TextsPath == ShownPath(file);
		const bool forThisDiff = uncommitted->TextsForDiff == uncommitted->Diff.GetApplied();
		const bool landed = uncommitted->TextsApplied == uncommitted->TextsRequested;

		return forThisFile && forThisDiff && landed ? uncommitted->Texts : nullptr;
	}

	void ReviewModel::StopUncommitted(const ReviewKey& key)
	{
		m_Uncommitted.erase(key);
	}
}
