#include "Git/DiffRows.h"

namespace Graphite {

	namespace {

		DiffRowKind KindOf(Core::DiffLineKind kind)
		{
			switch (kind)
			{
				case Core::DiffLineKind::Added: return DiffRowKind::Added;
				case Core::DiffLineKind::Removed: return DiffRowKind::Removed;
				case Core::DiffLineKind::Context: break;
			}

			return DiffRowKind::Context;
		}

		DiffRow RowFor(const Core::DiffLine& line, std::size_t hunk, std::size_t index)
		{
			DiffRow row;
			row.Kind = KindOf(line.Kind);
			row.OldLine = line.OldLine;
			row.NewLine = line.NewLine;
			row.Hunk = hunk;
			row.Line = index;
			return row;
		}

		DiffRow HeaderFor(std::size_t hunk)
		{
			DiffRow row;
			row.Kind = DiffRowKind::HunkHeader;
			row.Hunk = hunk;
			return row;
		}

		// Appends one hunk's own rows in the diff's order, header included. Shared by
		// BuildUnifiedRows and BuildWholeFileRows so the hunks-only and whole-file layouts
		// agree exactly on what a hunk looks like.
		void AppendUnifiedHunk(std::vector<DiffRow>& rows, const Core::FileDiff& file,
								std::size_t hunk)
		{
			rows.push_back(HeaderFor(hunk));

			const std::vector<Core::DiffLine>& lines = file.Hunks[hunk].Lines;

			for (std::size_t index = 0; index < lines.size(); ++index)
			{
				rows.push_back(RowFor(lines[index], hunk, index));
			}
		}

		// Appends one hunk's own pairs, zipping each run of removals against the run of
		// insertions that follows it. Shared with BuildWholeFileRows for the same reason as
		// AppendUnifiedHunk.
		void AppendSideBySideHunk(std::vector<DiffPair>& pairs, const Core::FileDiff& file,
								   std::size_t hunk)
		{
			pairs.push_back(DiffPair{HeaderFor(hunk), HeaderFor(hunk)});

			const std::vector<Core::DiffLine>& lines = file.Hunks[hunk].Lines;

			std::size_t index = 0;

			while (index < lines.size())
			{
				if (lines[index].Kind == Core::DiffLineKind::Context)
				{
					const DiffRow row = RowFor(lines[index], hunk, index);
					pairs.push_back(DiffPair{row, row});
					++index;
					continue;
				}

				// A run of removals followed by a run of insertions is git's way of
				// spelling "these lines became those lines". Zipping the two runs is what
				// turns that back into one row per changed line.
				const std::size_t removedFirst = index;

				while (index < lines.size() && lines[index].Kind == Core::DiffLineKind::Removed)
				{
					++index;
				}

				const std::size_t removedCount = index - removedFirst;
				const std::size_t addedFirst = index;

				while (index < lines.size() && lines[index].Kind == Core::DiffLineKind::Added)
				{
					++index;
				}

				const std::size_t addedCount = index - addedFirst;
				const std::size_t rows = removedCount > addedCount ? removedCount : addedCount;

				for (std::size_t offset = 0; offset < rows; ++offset)
				{
					DiffPair pair;

					if (offset < removedCount)
					{
						pair.Left = RowFor(lines[removedFirst + offset], hunk,
										   removedFirst + offset);
					}

					if (offset < addedCount)
					{
						pair.Right = RowFor(lines[addedFirst + offset], hunk,
											addedFirst + offset);
					}

					pairs.push_back(pair);
				}
			}
		}

		DiffRow UnchangedRow(int oldLine, int newLine)
		{
			DiffRow row;
			row.Kind = DiffRowKind::Unchanged;
			row.OldLine = oldLine;
			row.NewLine = newLine;
			return row;
		}
	}

	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file)
	{
		std::vector<DiffRow> rows;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			AppendUnifiedHunk(rows, file, hunk);
		}

		return rows;
	}

	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file)
	{
		std::vector<DiffPair> pairs;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			AppendSideBySideHunk(pairs, file, hunk);
		}

		return pairs;
	}

	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row)
	{
		if (row.Kind == DiffRowKind::HunkHeader || row.Hunk >= file.Hunks.size())
		{
			return nullptr;
		}

		const std::vector<Core::DiffLine>& lines = file.Hunks[row.Hunk].Lines;

		if (row.Line >= lines.size())
		{
			return nullptr;
		}

		return &lines[row.Line];
	}

	WholeFileRows BuildWholeFileRows(const Core::FileDiff& file, std::size_t oldLineCount,
									  std::size_t newLineCount)
	{
		WholeFileRows result;

		const bool hasOld = oldLineCount > 0;
		const bool hasNew = newLineCount > 0;

		// The next line, one-based like Core::DiffLine, that has not yet appeared in a row
		// on that side. Meaningless on a side the file does not have at all.
		std::size_t oldNext = 1;
		std::size_t newNext = 1;

		// Emits `gapLength` Unchanged rows starting from wherever each side left off, then
		// advances both pointers past them.
		auto emitGap = [&](std::size_t gapLength) {
			for (std::size_t offset = 0; offset < gapLength; ++offset)
			{
				const int oldLine = hasOld ? static_cast<int>(oldNext + offset) : 0;
				const int newLine = hasNew ? static_cast<int>(newNext + offset) : 0;

				const DiffRow row = UnchangedRow(oldLine, newLine);
				result.Unified.push_back(row);

				DiffPair pair;

				if (hasOld)
				{
					pair.Left = row;
				}

				if (hasNew)
				{
					pair.Right = row;
				}

				result.SideBySide.push_back(pair);
			}

			if (hasOld)
			{
				oldNext += gapLength;
			}

			if (hasNew)
			{
				newNext += gapLength;
			}
		};

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			const Core::Hunk& current = file.Hunks[hunk];

			// A side's Start is only well-defined by the unified diff format when that side's
			// own Count is non-zero; a zero-count side's Start names the line the other
			// side's change sits next to, not a line of its own. Preferring whichever side
			// this hunk actually has lines on keeps the gap length out of that ambiguity.
			std::size_t gapLength = 0;

			if (current.NewCount > 0)
			{
				gapLength = static_cast<std::size_t>(current.NewStart) - newNext;
			}
			else if (current.OldCount > 0)
			{
				gapLength = static_cast<std::size_t>(current.OldStart) - oldNext;
			}

			emitGap(gapLength);

			AppendUnifiedHunk(result.Unified, file, hunk);
			AppendSideBySideHunk(result.SideBySide, file, hunk);

			if (current.OldCount > 0)
			{
				oldNext = static_cast<std::size_t>(current.OldStart)
						  + static_cast<std::size_t>(current.OldCount);
			}

			if (current.NewCount > 0)
			{
				newNext = static_cast<std::size_t>(current.NewStart)
						  + static_cast<std::size_t>(current.NewCount);
			}
		}

		// The tail of the file past the last hunk (or the whole file, when there are none).
		// Both counts here come straight from the caller rather than a hunk header, so there
		// is no zero-count ambiguity to prefer one side over the other for.
		const std::size_t tailLength = hasNew ? (newLineCount + 1 - newNext)
											   : (hasOld ? (oldLineCount + 1 - oldNext) : 0);

		emitGap(tailLength);

		return result;
	}
}
