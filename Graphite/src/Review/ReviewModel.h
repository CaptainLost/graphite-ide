#pragma once

#include "Core/Base/Base.h"

#include "Git/Debouncer.h"
#include "Git/DeliveryInbox.h"
#include "Git/Diff/FileTexts.h"
#include "Git/GenerationSlot.h"
#include "Git/LruResultCache.h"
#include "Review/ReviewPort.h"
#include "Review/WorktreeWatch.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Jobs/JobSubmitter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Graphite {

	// What a range's diff is cached by: the same ends with and without white space are two answers.
	struct RangeDiffKey
	{
		RangeEnds Ends;
		Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;

		friend bool operator==(const RangeDiffKey&, const RangeDiffKey&) = default;
	};

	struct RangeDiffKeyHash
	{
		std::size_t operator()(const RangeDiffKey& key) const noexcept
		{
			const std::size_t endsHash = RangeEndsHash{}(key.Ends);
			const std::size_t whitespaceHash = static_cast<std::size_t>(key.Whitespace);
			return endsHash ^ (whitespaceHash << 1);
		}
	};

	// What the review tabs draw, and when it is stale. GitModel's rules hold here too: answers are
	// immutable and handed out by shared pointer, every request carries a generation, and an answer
	// becomes visible only in OnUpdate, on the UI thread.
	//
	// Every job captures `this`, so the queue behind the submitter must be destroyed, and its
	// workers joined, before this object is. An owner holding both declares this BEFORE the queue.
	class ReviewModel : Core::NonCopyable
	{
	public:
		ReviewModel(ReviewPort& port, Core::JobSubmitter submit, WorktreeWatchFactory watch);

		// Everything held is about the folder that was open.
		void SubscribeTo(Core::EventBus& events);

		void OnUpdate(Core::Timestep ts);
		void Clear();

		// Called on every frame a tab is drawn. Resolves the range the first time, and again once
		// InvalidateRanges or Retry marked it stale; one query at a time per review.
		void RequestRange(const ReviewKey& key);

		// Null until the first answer, and after a failure with nothing good before it.
		Core::Ref<const ReviewRange> GetRange(const ReviewKey& key) const;

		// Null unless the last answer was a failure.
		const Core::Error* GetRangeError(const ReviewKey& key) const;

		bool IsRangeLoading(const ReviewKey& key) const;

		// A branch moved: every review may now cover a different range. A review's uncommitted diff is
		// asked again only once the new range has moved its tip; the worktree's watch covers the rest.
		void InvalidateRanges();

		// The reader's Retry: the review's range and uncommitted diff are asked again, and the failed
		// answers held for its range - the diff and any file's texts - are forgotten.
		void Retry(const ReviewKey& key);

		// The tab closed or changed its base. An answer still in flight for it is dropped when it
		// lands, even if the same review is asked about again meanwhile.
		void Forget(const ReviewKey& key);

		// A range's diff never changes, so it is held once fetched. Asking about one already held,
		// in flight or failed does nothing; Retry is the way back from a failure.
		void RequestCommittedDiff(const RangeDiffKey& key);
		Core::Ref<const Core::Diff> GetCommittedDiff(const RangeDiffKey& key) const;
		bool IsCommittedDiffFailed(const RangeDiffKey& key) const;
		const std::optional<Core::Error>& GetCommittedDiffError() const { return m_CommittedDiffError; }

		// Keyed by the range and the path the list shows the file under. A binary file is never read.
		void RequestCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file);
		Core::Ref<const FileTexts> GetCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file) const;

		// Called on every frame a tab with the toggle on is drawn. The first call starts watching the
		// worktree, and a call naming another worktree starts over. A change under the worktree
		// marks the answer stale once the burst of changes settles.
		void RequestUncommitted(const ReviewKey& key, const std::filesystem::path& worktree,
								 Core::WhitespaceInDiff whitespace);
		Core::Ref<const Core::Diff> GetUncommitted(const ReviewKey& key) const;
		const Core::Error* GetUncommittedError(const ReviewKey& key) const;
		bool IsUncommittedLoading(const ReviewKey& key) const;

		// The texts of the one file on screen, read for the uncommitted diff now held: null while they
		// are on their way, once that diff is replaced by one that changes the file, after a failed read,
		// and for a binary file.
		void RequestUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file);
		Core::Ref<const FileTexts> GetUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file) const;

		// The toggle went off: the watch stops and what was held is dropped.
		void StopUncommitted(const ReviewKey& key);

		// A whole branch's diff is far larger than one commit's, so fewer are held than GitModel holds.
		static constexpr std::size_t kCachedDiffs = 8;
		static constexpr std::size_t kCachedTexts = 32;

	private:
		struct RangeState
		{
			// From a model-wide counter, so an answer can tell whether the review it was asked for is
			// still the one held under its key.
			std::uint64_t Incarnation = 0;
			GenerationSlot<ReviewRange> Slot;
			bool Asked = false;
			bool Stale = false;
		};

		struct TextsKey
		{
			RangeEnds Ends;
			std::filesystem::path Path;

			friend bool operator==(const TextsKey&, const TextsKey&) = default;
		};

		struct TextsKeyHash
		{
			std::size_t operator()(const TextsKey& key) const noexcept
			{
				const std::size_t endsHash = RangeEndsHash{}(key.Ends);
				const std::size_t pathHash = std::hash<std::filesystem::path>{}(key.Path);
				return endsHash ^ (pathHash << 1);
			}
		};

		struct UncommittedState
		{
			std::uint64_t Incarnation = 0;
			std::filesystem::path Worktree;
			Core::Scope<WorktreeWatch> Watch;
			Debouncer Debounce;
			GenerationSlot<Core::Diff> Diff;
			bool Asked = false;
			bool Stale = false;
			Core::WhitespaceInDiff Whitespace = Core::WhitespaceInDiff::Shown;

			std::filesystem::path TextsPath;
			std::uint64_t TextsForDiff = 0;
			std::uint64_t TextsRequested = 0;
			std::uint64_t TextsApplied = 0;
			Core::Ref<const FileTexts> Texts;
		};

		UncommittedState* FindUncommitted(const ReviewKey& key);
		const UncommittedState* FindUncommitted(const ReviewKey& key) const;
		UncommittedState* FindUncommittedOf(const ReviewKey& key, std::uint64_t incarnation);

		void LandUncommitted(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
							 Core::Result<Core::Diff> answer);
		void LandUncommittedTexts(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
								  Core::Result<FileTexts> answer);

		RangeState* FindRange(const ReviewKey& key, std::uint64_t incarnation);
		const RangeState* FindRange(const ReviewKey& key) const;

		void LandRange(const ReviewKey& key, std::uint64_t incarnation, std::uint64_t generation,
					   Core::Result<ReviewRange> answer);
		void LandCommittedDiff(const RangeDiffKey& key, std::uint64_t generation, Core::Result<Core::Diff> answer);
		void LandCommittedTexts(const TextsKey& key, std::uint64_t generation, Core::Result<FileTexts> answer);

		ReviewPort& m_Port;
		Core::JobSubmitter m_Submit;
		WorktreeWatchFactory m_Watch;
		DeliveryInbox m_Inbox;

		std::uint64_t m_NextIncarnation = 0;
		std::unordered_map<ReviewKey, RangeState, ReviewKeyHash> m_Ranges;

		LruResultCache<RangeDiffKey, Core::Diff, RangeDiffKeyHash> m_CommittedDiffs{kCachedDiffs};
		LruResultCache<TextsKey, FileTexts, TextsKeyHash> m_CommittedTexts{kCachedTexts};
		std::optional<Core::Error> m_CommittedDiffError;

		std::uint64_t m_NextTextsGeneration = 0;
		std::unordered_map<ReviewKey, UncommittedState, ReviewKeyHash> m_Uncommitted;
	};
}
