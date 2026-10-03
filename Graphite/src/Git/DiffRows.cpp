#include "Git/DiffRows.h"

#include "Git/CommitLabels.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		DiffRowKind KindOf(Core::DiffLineKind kind)
		{
			switch (kind)
			{
			case Core::DiffLineKind::Added:
				return DiffRowKind::Added;
			case Core::DiffLineKind::Removed:
				return DiffRowKind::Removed;
			case Core::DiffLineKind::Context:
				break;
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

		// A row of a hunk that names no line of it: the header, the scope under it, the gap.
		DiffRow MarkerFor(DiffRowKind kind, std::size_t hunk)
		{
			DiffRow row;
			row.Kind = kind;
			row.Hunk = hunk;
			return row;
		}

		// The rows that open a hunk in the hunks-only layout, which leaves out the lines between
		// hunks and so has to say where each one starts: a gap for what was left out before it
		// (none before the first), the header, and the scope git named, if it named one. The
		// whole-file layout leaves nothing out and has none of them.
		std::vector<DiffRow> MarkersFor(const Core::FileDiff& file, std::size_t hunk)
		{
			std::vector<DiffRow> markers;

			if (hunk > 0)
			{
				markers.push_back(MarkerFor(DiffRowKind::HunkGap, hunk));
			}

			markers.push_back(MarkerFor(DiffRowKind::HunkHeader, hunk));

			if (!HunkScope(file.Hunks[hunk].Header).empty())
			{
				markers.push_back(MarkerFor(DiffRowKind::HunkSignature, hunk));
			}

			return markers;
		}

		// Appends one hunk's own lines in the diff's order. Shared by BuildUnifiedRows and
		// BuildWholeFileRows so the hunks-only and whole-file layouts agree exactly on what a
		// hunk looks like.
		void AppendUnifiedHunk(std::vector<DiffRow>& rows, const Core::FileDiff& file,
							   std::size_t hunk)
		{
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

		// `end - next` as an unsigned difference, but clamped to zero rather than wrapping
		// when `next` has already passed `end`. That skew is only possible when a caller
		// hands in a line count from a different snapshot than the diff, or hunks that are
		// not in ascending order — DiffRows.h already warns a stale count can happen. Every
		// other place in this file degrades gracefully under that skew (LineOf just returns
		// null); an unclamped subtraction here would instead wrap to roughly 2^64 and have
		// emitGap push rows until memory runs out, turning a wrong picture into a hang.
		std::size_t ClampedGap(std::size_t end, std::size_t next)
		{
			return end > next ? end - next : 0;
		}
	}

	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file)
	{
		std::vector<DiffRow> rows;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			std::ranges::copy(MarkersFor(file, hunk), std::back_inserter(rows));
			AppendUnifiedHunk(rows, file, hunk);
		}

		return rows;
	}

	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file)
	{
		std::vector<DiffPair> pairs;

		for (std::size_t hunk = 0; hunk < file.Hunks.size(); ++hunk)
		{
			// Both halves of a marker say the same thing, each for its own side.
			for (const DiffRow& marker : MarkersFor(file, hunk))
			{
				pairs.push_back(DiffPair{marker, marker});
			}

			AppendSideBySideHunk(pairs, file, hunk);
		}

		return pairs;
	}

	const Core::DiffLine* LineOf(const Core::FileDiff& file, const DiffRow& row)
	{
		// A header, its scope or a gap names no line, and an Unchanged row names no hunk at all
		// — its Hunk and Line fields are left at their default zero, which would otherwise
		// resolve to file.Hunks[0].Lines[0] for any file whose first hunk is non-empty.
		if (row.Kind == DiffRowKind::HunkHeader || row.Kind == DiffRowKind::HunkSignature || row.Kind == DiffRowKind::HunkGap || row.Kind == DiffRowKind::Unchanged || row.Hunk >= file.Hunks.size())
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
				gapLength = ClampedGap(static_cast<std::size_t>(current.NewStart), newNext);
			}
			else if (current.OldCount > 0)
			{
				gapLength = ClampedGap(static_cast<std::size_t>(current.OldStart), oldNext);
			}

			emitGap(gapLength);

			AppendUnifiedHunk(result.Unified, file, hunk);
			AppendSideBySideHunk(result.SideBySide, file, hunk);

			if (current.OldCount > 0)
			{
				oldNext = static_cast<std::size_t>(current.OldStart) + static_cast<std::size_t>(current.OldCount);
			}

			if (current.NewCount > 0)
			{
				newNext = static_cast<std::size_t>(current.NewStart) + static_cast<std::size_t>(current.NewCount);
			}
		}

		// The tail of the file past the last hunk (or the whole file, when there are none).
		// Both counts here come straight from the caller rather than a hunk header, so there
		// is no zero-count ambiguity to prefer one side over the other for.
		const std::size_t tailLength = hasNew ? ClampedGap(newLineCount + 1, newNext)
											  : (hasOld ? ClampedGap(oldLineCount + 1, oldNext)
														: 0);

		emitGap(tailLength);

		return result;
	}

	namespace {

		// One whole-file line by its one-based number, with the tokens lexed for it. Absent
		// when the number names no line on that side — a side the file does not have leaves an
		// empty span, and a texts/rows disagreement about the file's length lands here rather
		// than out of bounds.
		ResolvedLine WholeFileLine(int number, std::span<const std::string_view> lines,
								   const std::vector<std::vector<Token>>& tokens)
		{
			if (number <= 0 || static_cast<std::size_t>(number) > lines.size())
			{
				return ResolvedLine{};
			}

			const std::size_t index = static_cast<std::size_t>(number) - 1;

			// The token vector is bounded separately: a lexer that produced fewer lines than
			// the split did leaves the tail uncoloured rather than reading past its end.
			return ResolvedLine{lines[index],
								index < tokens.size() ? &tokens[index] : nullptr};
		}

		const std::vector<Token>* TokensAt(int number,
										   const std::vector<std::vector<Token>>& tokens)
		{
			if (number <= 0 || static_cast<std::size_t>(number) > tokens.size())
			{
				return nullptr;
			}

			return &tokens[static_cast<std::size_t>(number) - 1];
		}
	}

	ResolvedLine ResolveRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
							std::span<const std::string_view> oldLines,
							std::span<const std::string_view> newLines,
							const FileTokens& tokens)
	{
		const bool newSide = side == DiffSide::New;

		if (row.Kind == DiffRowKind::Unchanged)
		{
			// Both halves of an Unchanged row carry identical content — the row exists only
			// where the diff left a line alone — so either side's text says the same thing. The
			// side being drawn still wins, because its tokens do not: a rename from foo.txt to
			// foo.cpp lexes each side with its own language, and colouring the old half from
			// the new file's lexer would claim a plain-text file was C++. The other side is the
			// fallback for a row only one side has a line for, which is how a file with no old
			// side (an add) or no new side (a delete) still resolves.
			const ResolvedLine preferred = newSide
											   ? WholeFileLine(row.NewLine, newLines, tokens.New)
											   : WholeFileLine(row.OldLine, oldLines, tokens.Old);

			if (preferred.Text.has_value())
			{
				return preferred;
			}

			return newSide ? WholeFileLine(row.OldLine, oldLines, tokens.Old)
						   : WholeFileLine(row.NewLine, newLines, tokens.New);
		}

		const Core::DiffLine* line = LineOf(file, row);

		if (line == nullptr)
		{
			return ResolvedLine{};
		}

		// A removed line exists only on the old side and an added line only on the new, so this
		// picks whichever side the diff itself says has the line; a context line has both, and
		// the side being drawn wins for the same reason as above.
		const std::vector<Token>* lineTokens =
			newSide ? TokensAt(row.NewLine, tokens.New) : TokensAt(row.OldLine, tokens.Old);

		if (lineTokens == nullptr)
		{
			lineTokens = newSide ? TokensAt(row.OldLine, tokens.Old)
								 : TokensAt(row.NewLine, tokens.New);
		}

		// The terminator comes off by the same rule SplitTextLines uses, so the bytes drawn for
		// a changed row and the bytes drawn for an Unchanged one are stripped identically.
		return ResolvedLine{StripLineTerminator(line->Text), lineTokens};
	}

	std::optional<DiffLineAnchor> AnchorOf(const DiffRow& row)
	{
		if (row.NewLine > 0)
		{
			return DiffLineAnchor{DiffSide::New, row.NewLine};
		}

		if (row.OldLine > 0)
		{
			return DiffLineAnchor{DiffSide::Old, row.OldLine};
		}

		return std::nullopt;
	}

	std::optional<DiffLineAnchor> AnchorOf(const DiffPair& pair)
	{
		if (pair.Right.has_value())
		{
			if (const std::optional<DiffLineAnchor> right = AnchorOf(*pair.Right))
			{
				return right;
			}
		}

		return pair.Left.has_value() ? AnchorOf(*pair.Left) : std::nullopt;
	}

	namespace {

		int LineOn(const DiffRow& row, DiffSide side)
		{
			return side == DiffSide::New ? row.NewLine : row.OldLine;
		}

		// A pair's old-side line lives in its left half and its new-side line in its right.
		int LineOn(const DiffPair& pair, DiffSide side)
		{
			const std::optional<DiffRow>& half = side == DiffSide::New ? pair.Right : pair.Left;
			return half.has_value() ? LineOn(*half, side) : 0;
		}

		// Shared by both layouts, which differ only in where a row keeps its line numbers. A
		// linear walk: it runs once per switch of layout, never per frame. Rows without a line
		// on the anchor's side — headers, gaps, and lines only the other side has — are passed
		// over, and among the rest each side's numbers only ever rise, which is what lets the
		// first one at or past the anchor stand for it.
		template<typename Row>
		DiffRowAt FindRow(std::span<const Row> rows, DiffLineAnchor anchor)
		{
			for (std::size_t index = 0; index < rows.size(); ++index)
			{
				const int line = LineOn(rows[index], anchor.Side);

				if (line <= 0 || line < anchor.Line)
				{
					continue;
				}

				if (line == anchor.Line)
				{
					return DiffRowAt{index, true};
				}

				// Back over whatever precedes it with no line on this side — the hunk's
				// header, the gap before it, and lines only the other side has — which belongs
				// before the line that was asked for and not after it. A line the hunks-only
				// rows leave out lands on the gap standing for it.
				while (index > 0 && LineOn(rows[index - 1], anchor.Side) <= 0)
				{
					--index;
				}

				return DiffRowAt{index, false};
			}

			return DiffRowAt{rows.empty() ? 0 : rows.size() - 1, false};
		}
	}

	namespace {

		bool IsUnchanged(const DiffRow& row)
		{
			return row.Kind == DiffRowKind::Unchanged;
		}

		// Both halves of an Unchanged pair are Unchanged, so either answers for the pair.
		bool IsUnchanged(const DiffPair& pair)
		{
			const std::optional<DiffRow>& half = pair.Right.has_value() ? pair.Right : pair.Left;
			return half.has_value() && IsUnchanged(*half);
		}

		template<typename Row>
		std::optional<std::size_t> FindRowToKeep(std::span<const Row> rows, std::size_t first,
												 std::size_t end)
		{
			end = std::min(end, rows.size());

			if (first >= end)
			{
				return std::nullopt;
			}

			// Doubled, so the middle of an even count sits between two rows without a fraction.
			const std::size_t middle = first + end - 1;

			std::optional<std::size_t> best;
			bool bestInHunk = false;
			std::size_t bestDistance = 0;

			for (std::size_t index = first; index < end; ++index)
			{
				if (!AnchorOf(rows[index]).has_value())
				{
					continue;
				}

				const bool inHunk = !IsUnchanged(rows[index]);
				const std::size_t twice = index * 2;
				const std::size_t distance = twice > middle ? twice - middle : middle - twice;

				if (!best.has_value() || (inHunk && !bestInHunk) || (inHunk == bestInHunk && distance < bestDistance))
				{
					best = index;
					bestInHunk = inHunk;
					bestDistance = distance;
				}
			}

			return best;
		}
	}

	std::optional<std::size_t> RowToKeep(std::span<const DiffRow> rows, std::size_t first,
										 std::size_t end)
	{
		return FindRowToKeep(rows, first, end);
	}

	std::optional<std::size_t> RowToKeep(std::span<const DiffPair> pairs, std::size_t first,
										 std::size_t end)
	{
		return FindRowToKeep(pairs, first, end);
	}

	DiffRowAt RowAt(std::span<const DiffRow> rows, DiffLineAnchor anchor)
	{
		return FindRow(rows, anchor);
	}

	DiffRowAt RowAt(std::span<const DiffPair> pairs, DiffLineAnchor anchor)
	{
		return FindRow(pairs, anchor);
	}

	DiffMeasurement MeasurementOf(const Core::FileDiff& file, DiffScope scope)
	{
		DiffMeasurement measurement;
		measurement.Path = ShownPath(file);
		measurement.Scope = scope;
		measurement.Hunks = file.Hunks.size();

		for (const Core::Hunk& hunk : file.Hunks)
		{
			measurement.Lines += hunk.Lines.size();
		}

		// The last line of the last hunk tells two diffs of equal shape apart: a file edited
		// near its top and the same file edited near its bottom agree on every count above.
		if (!file.Hunks.empty() && !file.Hunks.back().Lines.empty())
		{
			const Core::DiffLine& last = file.Hunks.back().Lines.back();
			measurement.LastOldLine = last.OldLine;
			measurement.LastNewLine = last.NewLine;
		}

		return measurement;
	}

	std::string_view HunkScope(std::string_view header)
	{
		constexpr std::string_view marker = "@@";

		if (!header.starts_with(marker))
		{
			return {};
		}

		const std::size_t close = header.find(marker, marker.size());

		if (close == std::string_view::npos)
		{
			return {};
		}

		std::string_view scope = header.substr(close + marker.size());

		const std::size_t first = scope.find_first_not_of(" \t");
		const std::size_t last = scope.find_last_not_of(" \t\r\n");

		if (first == std::string_view::npos || last == std::string_view::npos || last < first)
		{
			return {};
		}

		return scope.substr(first, last - first + 1);
	}

	std::string LineRangeLabel(int start, int count)
	{
		if (count <= 0)
		{
			return "—";
		}

		if (count == 1)
		{
			return std::to_string(start);
		}

		return std::to_string(start) + "–" + std::to_string(start + count - 1);
	}

	int SignatureLine(std::span<const std::string_view> lines, int before, std::string_view scope)
	{
		const auto unindented = [](std::string_view text) {
			const std::size_t first = text.find_first_not_of(" \t");
			return first == std::string_view::npos ? std::string_view{} : text.substr(first);
		};

		scope = unindented(scope);

		if (scope.empty() || before <= 1)
		{
			return 0;
		}

		const std::size_t start = std::min(static_cast<std::size_t>(before - 1), lines.size());

		for (std::size_t number = start; number >= 1; --number)
		{
			if (unindented(lines[number - 1]).starts_with(scope))
			{
				return static_cast<int>(number);
			}
		}

		return 0;
	}
}
