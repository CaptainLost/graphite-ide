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
#include <unordered_set>

namespace Graphite {

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

	// WARNING: Every job captures `this`, so the queue behind the submitter must be destroyed, and its
	// workers joined, before this object is. An owner holding both declares this BEFORE the queue.
	class ReviewModel : Core::NonCopyable
	{
	public:
		ReviewModel(ReviewPort& port, Core::JobSubmitter submit, WorktreeWatchFactory watch);

		void SubscribeTo(Core::EventBus& events);

		void OnUpdate(Core::Timestep timestep);
		void Clear();

		void RequestRange(const ReviewKey& key);

		Core::Ref<const ReviewRange> GetRange(const ReviewKey& key) const;

		const Core::Error* GetRangeError(const ReviewKey& key) const;

		bool IsRangeLoading(const ReviewKey& key) const;
		bool IsRangeRetrying(const ReviewKey& key) const;

		void InvalidateRanges();

		void Retry(const ReviewKey& key);

		void Forget(const ReviewKey& key);

		void RequestCommittedDiff(const RangeDiffKey& key);
		Core::Ref<const Core::Diff> GetCommittedDiff(const RangeDiffKey& key) const;
		bool IsCommittedDiffFailed(const RangeDiffKey& key) const;
		const std::optional<Core::Error>& GetCommittedDiffError() const { return m_CommittedDiffError; }

		void RequestCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file);
		Core::Ref<const FileTexts> GetCommittedTexts(const RangeEnds& ends, const Core::FileDiff& file) const;

		// What became of the texts asked for the file, while GetCommittedTexts holds none.
		TextsWait CommittedTextsWait(const RangeEnds& ends, const Core::FileDiff& file) const;

		void RequestSinceViewed(const SinceViewedRequest& request);
		Core::Ref<const SinceViewedAnswer> GetSinceViewed(const SinceViewedRequest& request) const;
		bool IsSinceViewedFailed(const SinceViewedRequest& request) const;
		bool IsSinceViewedUnavailable(const SinceViewedRequest& request) const;

		void RequestUncommitted(const ReviewKey& key, const std::filesystem::path& worktree,
								Core::WhitespaceInDiff whitespace);
		Core::Ref<const Core::Diff> GetUncommitted(const ReviewKey& key) const;
		const Core::Error* GetUncommittedError(const ReviewKey& key) const;
		bool IsUncommittedLoading(const ReviewKey& key) const;
		bool IsUncommittedRetrying(const ReviewKey& key) const;

		void RequestUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file);
		Core::Ref<const FileTexts> GetUncommittedTexts(const ReviewKey& key, const Core::FileDiff& file) const;

		// What became of the texts asked for the file, while GetUncommittedTexts holds none.
		TextsWait UncommittedTextsWait(const ReviewKey& key, const Core::FileDiff& file) const;

		void StopUncommitted(const ReviewKey& key);

		static constexpr std::size_t kCachedDiffs = 8;
		static constexpr std::size_t kCachedTexts = 32;

	private:
		// WARNING: A retry is told apart from a re-read in the background, which runs on every branch snapshot and worktree
		// change: only the reader's own Retry hides a failure behind a loader.
		struct RetryMark
		{
			bool Wanted = false;
			std::uint64_t Generation = 0;

			void NoteReadBegun(std::uint64_t generation)
			{
				if (Wanted)
				{
					Generation = generation;
					Wanted = false;
				}
			}

			bool IsUnanswered(std::uint64_t appliedGeneration) const
			{
				return Wanted || Generation > appliedGeneration;
			}
		};

		struct RangeState
		{
			std::uint64_t Incarnation = 0;
			GenerationSlot<ReviewRange> Slot;
			bool Asked = false;
			bool Stale = false;
			RetryMark Retry;
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
			RetryMark Retry;
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
		void LandSinceViewed(const SinceViewedKey& key, std::uint64_t generation, Core::Result<SinceViewedAnswer> answer);

		ReviewPort& m_Port;
		Core::JobSubmitter m_Submit;
		WorktreeWatchFactory m_Watch;
		DeliveryInbox m_Inbox;

		std::uint64_t m_NextIncarnation = 0;
		std::unordered_map<ReviewKey, RangeState, ReviewKeyHash> m_Ranges;

		LruResultCache<RangeDiffKey, Core::Diff, RangeDiffKeyHash> m_CommittedDiffs{kCachedDiffs};
		LruResultCache<TextsKey, FileTexts, TextsKeyHash> m_CommittedTexts{kCachedTexts};
		std::optional<Core::Error> m_CommittedDiffError;
		LruResultCache<SinceViewedKey, SinceViewedAnswer, SinceViewedKeyHash> m_SinceViewed{kCachedTexts};

		std::unordered_set<SinceViewedKey, SinceViewedKeyHash> m_ViewedVersionsGone;

		std::uint64_t m_NextTextsGeneration = 0;
		std::unordered_map<ReviewKey, UncommittedState, ReviewKeyHash> m_Uncommitted;
	};
}
