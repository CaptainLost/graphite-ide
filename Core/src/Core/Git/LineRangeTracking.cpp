#include "corepch.h"

#include "Core/Git/LineRangeTracking.h"

#include <algorithm>

namespace Core {

	namespace {

		// One past the last line a hunk covers on the new side. A hunk with no new lines
		// (a pure deletion) has `NewEnd == NewStart`, so it covers no new-side line at
		// all — `Overlaps` requires a strict `<` against this value, and `NewStart` itself
		// is excluded too.
		std::size_t NewEnd(const Hunk& hunk)
		{
			return static_cast<std::size_t>(hunk.NewStart)
				   + static_cast<std::size_t>(std::max(hunk.NewCount, 0));
		}

		bool Overlaps(const Hunk& hunk, const LineRange& range)
		{
			const std::size_t start = static_cast<std::size_t>(std::max(hunk.NewStart, 0));

			return start <= range.Last && NewEnd(hunk) > range.First;
		}

		// The old-side line that carried `pos` into this hunk unchanged, found by literal
		// line correspondence. A unified diff keeps a changed region's surrounding lines as
		// context on both sides, so a hunk can span a position it never actually edited —
		// this is what tells the two apart. Empty when the hunk carries no per-line detail,
		// which is how the hand-built hunks in this file's tests exercise the fallback path.
		std::optional<std::size_t> UnchangedOldLine(const Hunk& hunk, std::size_t pos)
		{
			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Context
					&& static_cast<std::size_t>(line.NewLine) == pos)
				{
					return static_cast<std::size_t>(line.OldLine);
				}
			}

			return std::nullopt;
		}

		// Whether this hunk edits something inside `range`, rather than merely carrying it
		// as context around an edit elsewhere. A removed line has no new-side line number
		// of its own — it is placed at the position right after the last line the hunk
		// kept, which is where the missing text would have been read.
		bool HunkChangesRange(const Hunk& hunk, const LineRange& range)
		{
			if (hunk.Lines.empty())
			{
				// No per-line detail to refine with: the whole hunk counts, matching the
				// coarse hunk-level overlap this falls back to.
				return true;
			}

			std::size_t lastNew = static_cast<std::size_t>(std::max(hunk.NewStart, 0));

			if (lastNew > 0)
			{
				--lastNew;
			}

			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Removed)
				{
					const std::size_t at = lastNew + 1;

					if (at >= range.First && at <= range.Last)
					{
						return true;
					}

					continue;
				}

				const std::size_t pos = static_cast<std::size_t>(line.NewLine);
				lastNew = pos;

				if (line.Kind == DiffLineKind::Added && pos >= range.First && pos <= range.Last)
				{
					return true;
				}
			}

			return false;
		}

		// Maps one new-side line number back through the file's hunks, assuming they are
		// in ascending order (as libgit2 always produces them within a file). Only hunks
		// entirely above `pos` shift it, by the difference between their two sides. A hunk
		// that reaches as far as `pos` stops the walk there: `pos` is resolved exactly when
		// it turns out to be unchanged context, and otherwise left alone rather than shifted
		// by a hunk that may not even apply to it — a position inside a hunk it was born in
		// has no parent-side answer worth guessing at.
		std::size_t MapPosition(const std::vector<Hunk>& hunks, std::size_t pos)
		{
			std::ptrdiff_t shift = 0;

			for (const Hunk& hunk : hunks)
			{
				if (NewEnd(hunk) <= pos)
				{
					shift += static_cast<std::ptrdiff_t>(hunk.OldCount)
							 - static_cast<std::ptrdiff_t>(hunk.NewCount);
					continue;
				}

				const std::size_t start = static_cast<std::size_t>(std::max(hunk.NewStart, 0));

				if (pos >= start)
				{
					if (const auto old = UnchangedOldLine(hunk, pos); old.has_value())
					{
						return *old;
					}
				}

				break;
			}

			const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(pos) + shift;
			return moved < 1 ? std::size_t{1} : static_cast<std::size_t>(moved);
		}
	}

	bool TouchesRange(const FileDiff& file, const LineRange& range)
	{
		if (file.Change == FileChange::Added || file.Change == FileChange::Deleted)
		{
			return true;
		}

		return std::any_of(file.Hunks.begin(), file.Hunks.end(), [&range](const Hunk& hunk) {
			return Overlaps(hunk, range) && HunkChangesRange(hunk, range);
		});
	}

	std::optional<LineRange> MapRangeToParent(const FileDiff& file, const LineRange& range)
	{
		// Nothing to map back to: the parent has no such file.
		if (file.Change == FileChange::Added)
		{
			return std::nullopt;
		}

		// Each endpoint is mapped independently: a hunk between them can shift the far
		// endpoint while leaving the near one untouched, which a single range-wide shift
		// cannot express.
		LineRange mapped;
		mapped.First = MapPosition(file.Hunks, range.First);
		mapped.Last = MapPosition(file.Hunks, range.Last);

		// A hunk that actually changes the range may have consumed old lines past what
		// `Last` mapped to — replacing three lines with one still means the two extra old
		// lines belong to this range's history. Only a hunk's removed lines can extend it:
		// a pure insertion contributed nothing on the old side to widen towards.
		std::size_t floorLast = 0;

		for (const Hunk& hunk : file.Hunks)
		{
			if (!Overlaps(hunk, range) || !HunkChangesRange(hunk, range))
			{
				continue;
			}

			for (const DiffLine& line : hunk.Lines)
			{
				if (line.Kind == DiffLineKind::Removed)
				{
					floorLast = std::max(floorLast, static_cast<std::size_t>(line.OldLine));
				}
			}
		}

		mapped.Last = std::max(mapped.Last, floorLast);

		if (mapped.Last < mapped.First)
		{
			mapped.Last = mapped.First;
		}

		return mapped;
	}
}
