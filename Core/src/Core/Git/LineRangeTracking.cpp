#include "corepch.h"

#include "Core/Git/LineRangeTracking.h"

#include <algorithm>

namespace Core {

	namespace {

		constexpr std::size_t kFirstLine = 1;

		std::size_t FirstNewLine(const Hunk& hunk)
		{
			return static_cast<std::size_t>(std::max(hunk.NewStart, 0));
		}

		std::size_t OnePastLastNewLine(const Hunk& hunk)
		{
			const std::size_t newStart = static_cast<std::size_t>(hunk.NewStart);
			const std::size_t newCount = static_cast<std::size_t>(std::max(hunk.NewCount, 0));
			return newStart + newCount;
		}

		std::size_t NewLineAboveHunk(const Hunk& hunk)
		{
			const std::size_t firstNewLine = FirstNewLine(hunk);

			if (firstNewLine == 0)
			{
				return 0;
			}

			return firstNewLine - 1;
		}

		bool RangeContains(const OneBasedLineRange& range, std::size_t line)
		{
			return line >= range.First && line <= range.Last;
		}

		// A pure deletion has OnePastLastNewLine == FirstNewLine, so the strict comparison gives
		// it no new-side line, not even NewStart.
		bool HunkOverlapsRange(const Hunk& hunk, const OneBasedLineRange& range)
		{
			return FirstNewLine(hunk) <= range.Last && OnePastLastNewLine(hunk) > range.First;
		}

		// A unified diff keeps context lines around an edit, so a hunk can span a line it
		// never changed. Only a context line says which old line carried it unchanged.
		std::optional<std::size_t> FindUnchangedOldLine(const Hunk& hunk, std::size_t newLine)
		{
			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Context && static_cast<std::size_t>(line.NewLine) == newLine)
				{
					return static_cast<std::size_t>(line.OldLine);
				}
			}

			return std::nullopt;
		}

		bool HunkChangesRange(const Hunk& hunk, const OneBasedLineRange& range)
		{
			const bool hasPerLineDetail = !hunk.Lines.empty();

			if (!hasPerLineDetail)
			{
				return true;
			}

			// A removed line has no new-side number of its own, so it is placed one past the
			// last new-side line seen, which is where its text would have been read.
			std::size_t lastNewSideLine = NewLineAboveHunk(hunk);

			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Removed)
				{
					const std::size_t removedLinePosition = lastNewSideLine + 1;

					if (RangeContains(range, removedLinePosition))
					{
						return true;
					}

					continue;
				}

				const std::size_t newLine = static_cast<std::size_t>(line.NewLine);
				lastNewSideLine = newLine;

				if (line.Kind == DiffLineKind::Added && RangeContains(range, newLine))
				{
					return true;
				}
			}

			return false;
		}

		bool HunkTouchesRange(const Hunk& hunk, const OneBasedLineRange& range)
		{
			return HunkOverlapsRange(hunk, range) && HunkChangesRange(hunk, range);
		}

		// Expects the hunks in ascending order, as libgit2 gives them. Only hunks wholly above
		// the line shift it; the first hunk that reaches it maps an unchanged context line
		// exactly and leaves any other line unshifted, since a line born in a hunk has no
		// parent-side answer worth guessing.
		std::size_t MapLineToParent(const std::vector<Hunk>& hunks, std::size_t newLine)
		{
			std::ptrdiff_t shiftToOldSide = 0;

			for (const Hunk& hunk : hunks)
			{
				const bool hunkIsAboveLine = OnePastLastNewLine(hunk) <= newLine;

				if (hunkIsAboveLine)
				{
					const std::ptrdiff_t oldCount = static_cast<std::ptrdiff_t>(hunk.OldCount);
					const std::ptrdiff_t newCount = static_cast<std::ptrdiff_t>(hunk.NewCount);
					shiftToOldSide += oldCount - newCount;
					continue;
				}

				if (newLine >= FirstNewLine(hunk))
				{
					const std::optional<std::size_t> unchangedOldLine = FindUnchangedOldLine(hunk, newLine);

					if (unchangedOldLine.has_value())
					{
						return *unchangedOldLine;
					}
				}

				break;
			}

			const std::ptrdiff_t shiftedLine = static_cast<std::ptrdiff_t>(newLine) + shiftToOldSide;
			const bool shiftedAboveFirstLine = shiftedLine < static_cast<std::ptrdiff_t>(kFirstLine);

			if (shiftedAboveFirstLine)
			{
				return kFirstLine;
			}

			return static_cast<std::size_t>(shiftedLine);
		}

		// The Removed and Added lines seen since the last Context line.
		struct ChangeRun
		{
			std::vector<std::size_t> RemovedOldLines;
			std::vector<std::size_t> AddedNewLines;
		};

		std::optional<TracedLine> TraceLineThroughRun(const ChangeRun& run, std::size_t line)
		{
			for (std::size_t position = 0; position < run.AddedNewLines.size(); ++position)
			{
				if (run.AddedNewLines[position] != line)
				{
					continue;
				}

				if (position < run.RemovedOldLines.size())
				{
					return TracedLine{.Fate = LineFate::Changed, .OldLine = run.RemovedOldLines[position]};
				}

				return TracedLine{.Fate = LineFate::Added};
			}

			return std::nullopt;
		}

		std::optional<TracedLine> TraceLineInHunk(const Hunk& hunk, std::size_t line)
		{
			ChangeRun run;

			for (const DiffLine& diffLine : hunk.Lines)
			{
				if (diffLine.Kind == DiffLineKind::Removed)
				{
					run.RemovedOldLines.push_back(static_cast<std::size_t>(diffLine.OldLine));
					continue;
				}

				if (diffLine.Kind == DiffLineKind::Added)
				{
					run.AddedNewLines.push_back(static_cast<std::size_t>(diffLine.NewLine));
					continue;
				}

				if (static_cast<std::size_t>(diffLine.NewLine) == line)
				{
					return TracedLine{.Fate = LineFate::Unchanged, .OldLine = static_cast<std::size_t>(diffLine.OldLine)};
				}

				// A Context line ends the run; the line cannot be in a run that is already over.
				if (const std::optional<TracedLine> traced = TraceLineThroughRun(run, line))
				{
					return traced;
				}

				run = ChangeRun{};
			}

			return TraceLineThroughRun(run, line);
		}
	}

	bool TouchesRange(const FileDiff& file, const OneBasedLineRange& range)
	{
		if (file.Change == FileChange::Added || file.Change == FileChange::Deleted)
		{
			return true;
		}

		for (const Hunk& hunk : file.Hunks)
		{
			if (HunkTouchesRange(hunk, range))
			{
				return true;
			}
		}

		return false;
	}

	std::optional<OneBasedLineRange> MapRangeToParent(const FileDiff& file, const OneBasedLineRange& range)
	{
		const bool fileIsNewInThisCommit = file.Change == FileChange::Added;

		if (fileIsNewInThisCommit)
		{
			return std::nullopt;
		}

		// Each end is mapped on its own, because a hunk between them shifts only the far one.
		OneBasedLineRange mapped{
			.First = MapLineToParent(file.Hunks, range.First),
			.Last = MapLineToParent(file.Hunks, range.Last),
		};

		std::size_t lastConsumedOldLine = 0;

		for (const Hunk& hunk : file.Hunks)
		{
			if (!HunkTouchesRange(hunk, range))
			{
				continue;
			}

			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Removed)
				{
					lastConsumedOldLine = std::max(lastConsumedOldLine, static_cast<std::size_t>(line.OldLine));
				}
			}
		}

		mapped.Last = std::max(mapped.Last, lastConsumedOldLine);

		if (mapped.Last < mapped.First)
		{
			mapped.Last = mapped.First;
		}

		return mapped;
	}

	TracedLine TraceLineToParent(const FileDiff& file, std::size_t line)
	{
		if (file.Change == FileChange::Added)
		{
			return TracedLine{.Fate = LineFate::Added};
		}

		std::ptrdiff_t shiftToOldSide = 0;

		for (const Hunk& hunk : file.Hunks)
		{
			if (OnePastLastNewLine(hunk) <= line)
			{
				shiftToOldSide += static_cast<std::ptrdiff_t>(hunk.OldCount) - static_cast<std::ptrdiff_t>(hunk.NewCount);
				continue;
			}

			if (line < FirstNewLine(hunk))
			{
				break;
			}

			if (const std::optional<TracedLine> traced = TraceLineInHunk(hunk, line))
			{
				return *traced;
			}

			break;
		}

		const std::ptrdiff_t shiftedLine = static_cast<std::ptrdiff_t>(line) + shiftToOldSide;
		return TracedLine{.Fate = LineFate::Unchanged,
						  .OldLine = static_cast<std::size_t>(std::max<std::ptrdiff_t>(shiftedLine, 0))};
	}
}
