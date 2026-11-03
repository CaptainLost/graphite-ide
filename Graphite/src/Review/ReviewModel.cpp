#include "Review/ReviewModel.h"

#include "Git/History/CommitLabels.h"
#include "Workspace/Workspace.h"

#include <optional>
#include <utility>

namespace Graphite {

	namespace {

		const Core::FileDiff* FindShownFile(const Core::Diff& diff, const std::filesystem::path& shownPath)
		{
			for (const Core::FileDiff& file : diff.Files)
			{
				if (ShownPath(file) == shownPath)
				{
					return &file;
				}
			}

			return nullptr;
		}

		bool SameFileInBoth(const Core::Diff& replaced, const Core::Diff& current, const std::filesystem::path& shownPath)
		{
			const Core::FileDiff* before = FindShownFile(replaced, shownPath);
			const Core::FileDiff* after = FindShownFile(current, shownPath);

			if (before == nullptr || after == nullptr)
			{
				return false;
			}

			return before->OldId == after->OldId && before->NewId == after->NewId;
		}

		std::optional<Core::Oid> TipOf(const ReviewRange* range)
		{
			if (range == nullptr || range->Problem != RangeProblem::None)
			{
				return std::nullopt;
			}

			return range->Ends.Tip;
		}

		bool SameCommitIds(const std::vector<Core::CommitSummary>& held, const std::vector<Core::CommitSummary>& answer)
		{
			if (held.size() != answer.size())
			{
				return false;
			}

			for (std::size_t commitIndex = 0; commitIndex < held.size(); ++commitIndex)
			{
				if (held[commitIndex].Id != answer[commitIndex].Id)
				{
					return false;
				}
			}

			return true;
		}

		bool IsSameRange(const ReviewRange& held, const ReviewRange& answer)
		{
			const bool sameEnds = held.Problem == answer.Problem && held.Ends == answer.Ends;
			const bool sameWorktree = held.Worktree == answer.Worktree;
			const bool sameCommits =
				held.CommitsComplete == answer.CommitsComplete && SameCommitIds(held.Commits, answer.Commits);

			return sameEnds && sameWorktree && sameCommits;
		}

		bool IsAnySinceViewedKey(const SinceViewedKey&)
		{
			return true;
		}
	}

	ReviewModel::ReviewModel(ReviewPort& port, Core::JobSubmitter submit, WorktreeWatchFactory watch)
		: m_Port(port), m_Submit(std::move(submit)), m_Watch(std::move(watch))
	{
	}

	void ReviewModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });
	}

	void ReviewModel::OnUpdate(Core::Timestep timestep)
	{
		for (auto& [key, uncommitted] : m_Uncommitted)
		{
			const bool worktreeChanged = uncommitted.Watch != nullptr && uncommitted.Watch->TakeChanged();

			if (worktreeChanged)
			{
				uncommitted.Debounce.Mark();
			}

			if (uncommitted.Debounce.Advance(timestep))
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
		m_SinceViewed.Clear();
		m_ViewedVersionsGone.clear();
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
		range.Retry.NoteReadBegun(generation);

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

		// WARNING: Branches are read again on every save, and a new pointer for the same range would
		// have every badge and tab work out again what they show.
		const Core::Ref<const ReviewRange> replaced = range->Slot.Get();
		const bool answerIsUnchanged = answer.HasValue() && replaced != nullptr && IsSameRange(*replaced, answer.GetValue());
		const SlotLanding landing = answerIsUnchanged ? range->Slot.LandSameAsHeld(generation)
													  : range->Slot.Land(generation, std::move(answer));

		if (landing != SlotLanding::Applied)
		{
			return;
		}

		const bool tipMoved = TipOf(replaced.get()) != TipOf(range->Slot.Get().get());
		UncommittedState* uncommitted = FindUncommitted(key);

		if (tipMoved && uncommitted != nullptr)
		{
			uncommitted->Stale = true;
		}
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

	bool ReviewModel::IsRangeRetrying(const ReviewKey& key) const
	{
		const RangeState* range = FindRange(key);
		return range != nullptr && range->Retry.IsUnanswered(range->Slot.GetApplied());
	}

	void ReviewModel::InvalidateRanges()
	{
		for (auto& [key, range] : m_Ranges)
		{
			range.Stale = true;
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
			range.Retry.Wanted = true;

			if (const Core::Ref<const ReviewRange> held = range.Slot.Get(); held != nullptr)
			{
				const RangeDiffKey shownKey{.Ends = held->Ends, .Whitespace = Core::WhitespaceInDiff::Shown};
				const RangeDiffKey ignoredKey{.Ends = held->Ends, .Whitespace = Core::WhitespaceInDiff::Ignored};
				m_CommittedDiffs.ForgetFailed(shownKey);
				m_CommittedDiffs.ForgetFailed(ignoredKey);

				const RangeEnds ends = held->Ends;
				m_CommittedTexts.ForgetFailedWhere([&ends](const TextsKey& texts) { return texts.Ends == ends; });
			}
		}

		m_SinceViewed.ForgetFailedWhere(IsAnySinceViewedKey);
		m_ViewedVersionsGone.clear();

		UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted != nullptr)
		{
			uncommitted->Stale = true;
			uncommitted->Retry.Wanted = true;
		}
	}

	void ReviewModel::Forget(const ReviewKey& key)
	{
		m_Ranges.erase(key);
		m_Uncommitted.erase(key);
	}

	void ReviewModel::RequestCommittedDiff(const RangeDiffKey& key)
	{
		const std::optional<std::uint64_t> generation = m_CommittedDiffs.Request(key);

		if (!generation.has_value())
		{
			return;
		}

		m_Submit([this, key, generation = *generation]() {
			Core::Result<Core::Diff> answer = m_Port.QueryRangeDiff(key.Ends, key.Whitespace);

			m_Inbox.Post([this, key, generation, answer = std::move(answer)]() mutable {
				LandCommittedDiff(key, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandCommittedDiff(const RangeDiffKey& key, std::uint64_t generation,
										Core::Result<Core::Diff> answer)
	{
		if (!answer.HasValue())
		{
			m_CommittedDiffs.Fail(key, generation, answer.GetError());
			return;
		}

		Core::Ref<const Core::Diff> diff = Core::CreateRef<const Core::Diff>(std::move(answer).GetValue());

		m_CommittedDiffs.Succeed(key, generation, std::move(diff));
	}

	Core::Ref<const Core::Diff> ReviewModel::GetCommittedDiff(const RangeDiffKey& key) const
	{
		return m_CommittedDiffs.Get(key);
	}

	bool ReviewModel::IsCommittedDiffFailed(const RangeDiffKey& key) const
	{
		return m_CommittedDiffs.IsFailed(key);
	}

	std::optional<Core::Error> ReviewModel::GetCommittedDiffFailure(const RangeDiffKey& key) const
	{
		return m_CommittedDiffs.FailureOf(key);
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
		// WARNING: A text that cannot be read costs the pane its colouring and whole-file view while the
		// diff still draws, so the failure is held to stop a retry per frame and is not reported in a toast;
		// the diff's toolbar says it.
		if (!answer.HasValue())
		{
			m_CommittedTexts.Fail(key, generation, answer.GetError());
			return;
		}

		Core::Ref<const FileTexts> texts = Core::CreateRef<const FileTexts>(std::move(answer).GetValue());
		m_CommittedTexts.Succeed(key, generation, std::move(texts));
	}

	Core::Ref<const FileTexts> ReviewModel::GetCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file) const
	{
		return m_CommittedTexts.Get(TextsKey{.Ends = ends, .Path = ShownPath(file)});
	}

	TextsWait ReviewModel::CommittedTextsWait(const RangeEnds& ends, const Core::FileDiff& file) const
	{
		const TextsKey key{.Ends = ends, .Path = ShownPath(file)};

		if (file.IsBinary || m_CommittedTexts.Get(key) != nullptr)
		{
			return TextsWait{};
		}

		if (!m_CommittedTexts.IsFailed(key))
		{
			return TextsWait{.Progress = TextsProgress::Loading};
		}

		const std::optional<Core::Error> failure = m_CommittedTexts.FailureOf(key);

		return TextsWait{.Progress = TextsProgress::Failed,
						 .Reason = failure.has_value() ? failure->Message : std::string{}};
	}

	void ReviewModel::RequestSinceViewed(const SinceViewedRequest& request)
	{
		const SinceViewedKey key = KeyOf(request);
		const std::optional<std::uint64_t> generation = m_SinceViewed.Request(key);

		if (!generation.has_value())
		{
			return;
		}

		m_ViewedVersionsGone.erase(key);

		m_Submit([this, key, request, generation = *generation]() {
			Core::Result<SinceViewedAnswer> answer = m_Port.ReadSinceViewed(request);

			if (answer.HasValue())
			{
				SinceViewedAnswer& since = answer.GetValue();
				since.Texts.Tokens = ColourSides(since.Texts, request.Path, request.Path);
			}

			m_Inbox.Post([this, key, generation, answer = std::move(answer)]() mutable {
				LandSinceViewed(key, generation, std::move(answer));
			});
		});
	}

	void ReviewModel::LandSinceViewed(const SinceViewedKey& key, std::uint64_t generation,
									  Core::Result<SinceViewedAnswer> answer)
	{
		if (!answer.HasValue())
		{
			const bool failureLanded = m_SinceViewed.Fail(key, generation);
			const bool viewedVersionIsGone = answer.GetError().Code == Core::ErrorCode::NotFound;

			if (failureLanded && viewedVersionIsGone)
			{
				m_ViewedVersionsGone.insert(key);
			}

			return;
		}

		Core::Ref<const SinceViewedAnswer> since = Core::CreateRef<const SinceViewedAnswer>(std::move(answer).GetValue());
		m_SinceViewed.Succeed(key, generation, std::move(since));
	}

	Core::Ref<const SinceViewedAnswer> ReviewModel::GetSinceViewed(const SinceViewedRequest& request) const
	{
		return m_SinceViewed.Get(KeyOf(request));
	}

	bool ReviewModel::IsSinceViewedFailed(const SinceViewedRequest& request) const
	{
		return m_SinceViewed.IsFailed(KeyOf(request));
	}

	bool ReviewModel::IsSinceViewedUnavailable(const SinceViewedRequest& request) const
	{
		const SinceViewedKey key = KeyOf(request);
		return m_SinceViewed.IsFailed(key) && m_ViewedVersionsGone.contains(key);
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

	void ReviewModel::RequestUncommitted(const ReviewKey& key, const std::filesystem::path& worktree,
										 Core::WhitespaceInDiff whitespace)
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

		if (uncommitted.Whitespace != whitespace)
		{
			uncommitted.Whitespace = whitespace;
			uncommitted.Stale = true;
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
		uncommitted.Retry.NoteReadBegun(generation);

		m_Submit([this, key, worktree, whitespace, generation, incarnation]() {
			Core::Result<Core::Diff> answer = m_Port.QueryWorktreeDiff(worktree, whitespace);

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

		const Core::Ref<const Core::Diff> replaced = uncommitted->Diff.Get();
		const std::uint64_t replacedGeneration = uncommitted->Diff.GetApplied();

		const SlotLanding landing = uncommitted->Diff.Land(generation, std::move(answer));

		if (landing != SlotLanding::Applied)
		{
			return;
		}

		const bool textsLanded = uncommitted->TextsRequested != 0 && uncommitted->TextsApplied == uncommitted->TextsRequested;
		const bool textsBelongToReplaced = textsLanded && uncommitted->TextsForDiff == replacedGeneration;

		if (textsBelongToReplaced && replaced != nullptr && uncommitted->Texts != nullptr &&
			SameFileInBoth(*replaced, *uncommitted->Diff.Get(), uncommitted->TextsPath))
		{
			uncommitted->TextsForDiff = uncommitted->Diff.GetApplied();
		}
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

	bool ReviewModel::IsUncommittedRetrying(const ReviewKey& key) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);
		return uncommitted != nullptr && uncommitted->Retry.IsUnanswered(uncommitted->Diff.GetApplied());
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

	TextsWait ReviewModel::UncommittedTextsWait(const ReviewKey& key, const Core::FileDiff& file) const
	{
		const UncommittedState* uncommitted = FindUncommitted(key);

		if (uncommitted == nullptr || file.IsBinary)
		{
			return TextsWait{};
		}

		const bool forThisFile = uncommitted->TextsPath == ShownPath(file);
		const bool forThisDiff = uncommitted->TextsForDiff == uncommitted->Diff.GetApplied();
		const bool landed = uncommitted->TextsApplied == uncommitted->TextsRequested;

		if (!forThisFile || !forThisDiff || !landed)
		{
			return TextsWait{.Progress = TextsProgress::Loading};
		}

		return uncommitted->Texts == nullptr ? TextsWait{.Progress = TextsProgress::Failed} : TextsWait{};
	}

	void ReviewModel::StopUncommitted(const ReviewKey& key)
	{
		m_Uncommitted.erase(key);
	}
}
