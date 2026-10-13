#pragma once

#include "Review/ReviewPort.h"
#include "Review/WorktreeWatch.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

// Answers every question from its fields and counts how often each was asked. ResolveRange hands
// out RangeAnswers in order while any are left, then Range.
class FakeReviewPort final : public Graphite::ReviewPort
{
public:
	Core::Result<Graphite::ReviewRange> ResolveRange(const Graphite::ReviewKey&) override
	{
		++RangeQueries;

		if (RangeFailure.has_value())
		{
			return *RangeFailure;
		}

		if (m_NextAnswer < RangeAnswers.size())
		{
			++m_NextAnswer;
			return RangeAnswers[m_NextAnswer - 1];
		}

		return Range;
	}

	Core::Result<Core::Diff> QueryRangeDiff(const Graphite::RangeEnds&) override
	{
		++RangeDiffQueries;

		if (RangeDiffFailure.has_value())
		{
			return *RangeDiffFailure;
		}

		return RangeDiff;
	}

	Core::Result<Graphite::FileTexts> ReadRangeTexts(const Graphite::RangeEnds&, const std::filesystem::path&,
													 const std::filesystem::path&) override
	{
		++RangeTextReads;
		return Texts;
	}

	Core::Result<Core::Diff> QueryWorktreeDiff(const std::filesystem::path& worktree) override
	{
		++WorktreeDiffQueries;

		if (WorktreeFailure.has_value())
		{
			return *WorktreeFailure;
		}

		const std::map<std::filesystem::path, Core::Diff>::const_iterator own = WorktreeDiffsByPath.find(worktree);

		if (own != WorktreeDiffsByPath.end())
		{
			return own->second;
		}

		return WorktreeDiff;
	}

	Core::Result<Graphite::FileTexts> ReadWorktreeTexts(const std::filesystem::path&, const std::filesystem::path&,
														const std::filesystem::path&) override
	{
		++WorktreeTextReads;
		return Texts;
	}

	Graphite::ReviewRange Range;
	std::vector<Graphite::ReviewRange> RangeAnswers;
	std::optional<Core::Error> RangeFailure;

	Core::Diff RangeDiff;
	std::optional<Core::Error> RangeDiffFailure;

	Core::Diff WorktreeDiff;
	std::optional<Core::Error> WorktreeFailure;
	std::map<std::filesystem::path, Core::Diff> WorktreeDiffsByPath;

	Graphite::FileTexts Texts;

	int RangeQueries = 0;
	int RangeDiffQueries = 0;
	int RangeTextReads = 0;
	int WorktreeDiffQueries = 0;
	int WorktreeTextReads = 0;

private:
	std::size_t m_NextAnswer = 0;
};

inline Graphite::WorktreeWatchFactory NoWatches()
{
	return [](const std::filesystem::path&) -> Core::Scope<Graphite::WorktreeWatch> { return nullptr; };
}
