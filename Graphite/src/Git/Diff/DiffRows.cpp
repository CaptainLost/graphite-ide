#include "Git/Diff/DiffRows.h"

#include "Core/Base/Ascii.h"

#include "Git/History/CommitLabels.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr std::string_view kIndentation = " \t";
		constexpr std::string_view kTrailingWhitespace = " \t\r\n";
		constexpr std::string_view kHunkMarker = "@@";

		DiffRowKind RowKindOf(Core::DiffLineKind kind)
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

		DiffRow RowFor(const Core::DiffLine& line, std::size_t hunkIndex, std::size_t lineIndex)
		{
			DiffRow row;
			row.Kind = RowKindOf(line.Kind);
			row.OldLine = line.OldLine;
			row.NewLine = line.NewLine;
			row.Hunk = hunkIndex;
			row.Line = lineIndex;
			return row;
		}

		DiffRow MarkerFor(DiffRowKind kind, std::size_t hunkIndex)
		{
			DiffRow row;
			row.Kind = kind;
			row.Hunk = hunkIndex;
			return row;
		}

		std::vector<DiffRow> MarkersFor(const Core::FileDiff& file, std::size_t hunkIndex)
		{
			std::vector<DiffRow> markers;

			if (hunkIndex > 0)
			{
				markers.push_back(MarkerFor(DiffRowKind::HunkGap, hunkIndex));
			}

			markers.push_back(MarkerFor(DiffRowKind::HunkHeader, hunkIndex));

			if (!HunkScope(file.Hunks[hunkIndex].Header).empty())
			{
				markers.push_back(MarkerFor(DiffRowKind::HunkSignature, hunkIndex));
			}

			return markers;
		}

		void AppendUnifiedHunk(std::vector<DiffRow>& rows, const Core::FileDiff& file,
							   std::size_t hunkIndex)
		{
			const std::vector<Core::DiffLine>& lines = file.Hunks[hunkIndex].Lines;

			for (std::size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
			{
				rows.push_back(RowFor(lines[lineIndex], hunkIndex, lineIndex));
			}
		}

		void AppendSideBySideHunk(std::vector<DiffPair>& pairs, const Core::FileDiff& file,
								  std::size_t hunkIndex)
		{
			const std::vector<Core::DiffLine>& lines = file.Hunks[hunkIndex].Lines;

			std::size_t lineIndex = 0;

			while (lineIndex < lines.size())
			{
				if (lines[lineIndex].Kind == Core::DiffLineKind::Context)
				{
					const DiffRow row = RowFor(lines[lineIndex], hunkIndex, lineIndex);
					pairs.push_back(DiffPair{row, row});
					++lineIndex;
					continue;
				}

				const std::size_t firstRemovedIndex = lineIndex;

				while (lineIndex < lines.size() &&
					   lines[lineIndex].Kind == Core::DiffLineKind::Removed)
				{
					++lineIndex;
				}

				const std::size_t removedCount = lineIndex - firstRemovedIndex;
				const std::size_t firstAddedIndex = lineIndex;

				while (lineIndex < lines.size() &&
					   lines[lineIndex].Kind == Core::DiffLineKind::Added)
				{
					++lineIndex;
				}

				const std::size_t addedCount = lineIndex - firstAddedIndex;
				const std::size_t pairCount = removedCount > addedCount ? removedCount : addedCount;

				for (std::size_t pairOffset = 0; pairOffset < pairCount; ++pairOffset)
				{
					DiffPair pair;

					if (pairOffset < removedCount)
					{
						pair.Left = RowFor(lines[firstRemovedIndex + pairOffset], hunkIndex,
										   firstRemovedIndex + pairOffset);
					}

					if (pairOffset < addedCount)
					{
						pair.Right = RowFor(lines[firstAddedIndex + pairOffset], hunkIndex,
											firstAddedIndex + pairOffset);
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

		// WARNING: Clamps to zero instead of wrapping when `nextLine` is already past `endLine`,
		// which a line count from a different snapshot than the diff can cause. An unsigned
		// wrap would make the caller emit rows until memory runs out.
		std::size_t ClampedGap(std::size_t endLine, std::size_t nextLine)
		{
			return endLine > nextLine ? endLine - nextLine : 0;
		}

		bool NamesNoLine(DiffRowKind kind)
		{
			return kind == DiffRowKind::HunkHeader || kind == DiffRowKind::HunkSignature ||
				   kind == DiffRowKind::HunkGap || kind == DiffRowKind::Unchanged;
		}

		struct WholeFileCursor
		{
			bool HasOld = false;
			bool HasNew = false;
			std::size_t OldNext = 1;
			std::size_t NewNext = 1;
		};

		void AppendUnchangedRows(WholeFileRows& wholeFile, WholeFileCursor& cursor,
								 std::size_t rowCount)
		{
			for (std::size_t rowOffset = 0; rowOffset < rowCount; ++rowOffset)
			{
				const int oldLine = cursor.HasOld ? static_cast<int>(cursor.OldNext + rowOffset) : 0;
				const int newLine = cursor.HasNew ? static_cast<int>(cursor.NewNext + rowOffset) : 0;

				const DiffRow row = UnchangedRow(oldLine, newLine);
				wholeFile.Unified.push_back(row);

				DiffPair pair;

				if (cursor.HasOld)
				{
					pair.Left = row;
				}

				if (cursor.HasNew)
				{
					pair.Right = row;
				}

				wholeFile.SideBySide.push_back(pair);
			}

			if (cursor.HasOld)
			{
				cursor.OldNext += rowCount;
			}

			if (cursor.HasNew)
			{
				cursor.NewNext += rowCount;
			}
		}
	}

	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file)
	{
		std::vector<DiffRow> rows;

		for (std::size_t hunkIndex = 0; hunkIndex < file.Hunks.size(); ++hunkIndex)
		{
			std::ranges::copy(MarkersFor(file, hunkIndex), std::back_inserter(rows));
			AppendUnifiedHunk(rows, file, hunkIndex);
		}

		return rows;
	}

	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file)
	{
		std::vector<DiffPair> pairs;

		for (std::size_t hunkIndex = 0; hunkIndex < file.Hunks.size(); ++hunkIndex)
		{
			for (const DiffRow& marker : MarkersFor(file, hunkIndex))
			{
				pairs.push_back(DiffPair{marker, marker});
			}

			AppendSideBySideHunk(pairs, file, hunkIndex);
		}

		return pairs;
	}

	const Core::DiffLine* DiffLineOf(const Core::FileDiff& file, const DiffRow& row)
	{
		if (NamesNoLine(row.Kind) || row.Hunk >= file.Hunks.size())
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
		WholeFileRows wholeFile;

		WholeFileCursor cursor;
		cursor.HasOld = oldLineCount > 0;
		cursor.HasNew = newLineCount > 0;

		for (std::size_t hunkIndex = 0; hunkIndex < file.Hunks.size(); ++hunkIndex)
		{
			const Core::Hunk& currentHunk = file.Hunks[hunkIndex];

			// WARNING: A side's Start is well defined only when that side's own Count is
			// non-zero. A zero-count side's Start names the line the other side's change sits
			// next to, so the gap length comes from whichever side the hunk has lines on.
			std::size_t gapLength = 0;

			if (currentHunk.NewCount > 0)
			{
				gapLength = ClampedGap(static_cast<std::size_t>(currentHunk.NewStart), cursor.NewNext);
			}
			else if (currentHunk.OldCount > 0)
			{
				gapLength = ClampedGap(static_cast<std::size_t>(currentHunk.OldStart), cursor.OldNext);
			}

			AppendUnchangedRows(wholeFile, cursor, gapLength);

			AppendUnifiedHunk(wholeFile.Unified, file, hunkIndex);
			AppendSideBySideHunk(wholeFile.SideBySide, file, hunkIndex);

			if (currentHunk.OldCount > 0)
			{
				cursor.OldNext = static_cast<std::size_t>(currentHunk.OldStart) +
								 static_cast<std::size_t>(currentHunk.OldCount);
			}

			if (currentHunk.NewCount > 0)
			{
				cursor.NewNext = static_cast<std::size_t>(currentHunk.NewStart) +
								 static_cast<std::size_t>(currentHunk.NewCount);
			}
		}

		std::size_t tailLength = 0;

		if (cursor.HasNew)
		{
			tailLength = ClampedGap(newLineCount + 1, cursor.NewNext);
		}
		else if (cursor.HasOld)
		{
			tailLength = ClampedGap(oldLineCount + 1, cursor.OldNext);
		}

		AppendUnchangedRows(wholeFile, cursor, tailLength);

		return wholeFile;
	}

	namespace {

		ResolvedLine WholeFileLine(int lineNumber, std::span<const std::string_view> lines,
								   const std::vector<std::vector<Token>>& lexedTokens)
		{
			if (lineNumber <= 0 || static_cast<std::size_t>(lineNumber) > lines.size())
			{
				return ResolvedLine{};
			}

			const std::size_t lineIndex = static_cast<std::size_t>(lineNumber) - 1;

			return ResolvedLine{lines[lineIndex],
								lineIndex < lexedTokens.size() ? &lexedTokens[lineIndex] : nullptr};
		}

		const std::vector<Token>* TokensAt(int lineNumber,
										   const std::vector<std::vector<Token>>& lexedTokens)
		{
			if (lineNumber <= 0 || static_cast<std::size_t>(lineNumber) > lexedTokens.size())
			{
				return nullptr;
			}

			return &lexedTokens[static_cast<std::size_t>(lineNumber) - 1];
		}
	}

	// WARNING: The side being drawn wins even though both halves of an Unchanged row hold the
	// same text. A rename from foo.txt to foo.cpp lexes each side with its own language, so
	// colouring the old half from the new file's tokens would be wrong.
	ResolvedLine ResolveRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
							std::span<const std::string_view> oldLines,
							std::span<const std::string_view> newLines,
							const FileTokens& tokens)
	{
		const bool drawsNewSide = side == DiffSide::New;

		if (row.Kind == DiffRowKind::Unchanged)
		{
			const ResolvedLine drawnSideLine =
				drawsNewSide ? WholeFileLine(row.NewLine, newLines, tokens.New)
							 : WholeFileLine(row.OldLine, oldLines, tokens.Old);

			if (drawnSideLine.Text.has_value())
			{
				return drawnSideLine;
			}

			return drawsNewSide ? WholeFileLine(row.OldLine, oldLines, tokens.Old)
								: WholeFileLine(row.NewLine, newLines, tokens.New);
		}

		const Core::DiffLine* line = DiffLineOf(file, row);

		if (line == nullptr)
		{
			return ResolvedLine{};
		}

		const std::vector<Token>* lineTokens =
			drawsNewSide ? TokensAt(row.NewLine, tokens.New) : TokensAt(row.OldLine, tokens.Old);

		if (lineTokens == nullptr)
		{
			lineTokens = drawsNewSide ? TokensAt(row.OldLine, tokens.Old)
									  : TokensAt(row.NewLine, tokens.New);
		}

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
			if (const std::optional<DiffLineAnchor> rightAnchor = AnchorOf(*pair.Right))
			{
				return rightAnchor;
			}
		}

		return pair.Left.has_value() ? AnchorOf(*pair.Left) : std::nullopt;
	}

	namespace {

		int LineOn(const DiffRow& row, DiffSide side)
		{
			return side == DiffSide::New ? row.NewLine : row.OldLine;
		}

		int LineOn(const DiffPair& pair, DiffSide side)
		{
			const std::optional<DiffRow>& sideHalf = side == DiffSide::New ? pair.Right : pair.Left;
			return sideHalf.has_value() ? LineOn(*sideHalf, side) : 0;
		}

		// WARNING: A linear walk, so call it once per switch of layout and never per frame.
		template<typename Row>
		DiffRowAt FindRow(std::span<const Row> rows, DiffLineAnchor anchor)
		{
			for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
			{
				const int lineNumber = LineOn(rows[rowIndex], anchor.Side);

				if (lineNumber <= 0 || lineNumber < anchor.Line)
				{
					continue;
				}

				if (lineNumber == anchor.Line)
				{
					return DiffRowAt{rowIndex, true};
				}

				std::size_t landingIndex = rowIndex;

				while (landingIndex > 0 && LineOn(rows[landingIndex - 1], anchor.Side) <= 0)
				{
					--landingIndex;
				}

				return DiffRowAt{landingIndex, false};
			}

			return DiffRowAt{rows.empty() ? 0 : rows.size() - 1, false};
		}
	}

	namespace {

		bool IsUnchanged(const DiffRow& row)
		{
			return row.Kind == DiffRowKind::Unchanged;
		}

		bool IsUnchanged(const DiffPair& pair)
		{
			const std::optional<DiffRow>& presentHalf = pair.Right.has_value() ? pair.Right : pair.Left;
			return presentHalf.has_value() && IsUnchanged(*presentHalf);
		}

		template<typename Row>
		std::optional<std::size_t> FindRowToKeep(std::span<const Row> rows, std::size_t firstIndex,
												 std::size_t endIndex)
		{
			endIndex = std::min(endIndex, rows.size());

			if (firstIndex >= endIndex)
			{
				return std::nullopt;
			}

			const std::size_t doubledMiddle = firstIndex + endIndex - 1;

			std::optional<std::size_t> bestIndex;
			bool bestIsInHunk = false;
			std::size_t bestDoubledDistance = 0;

			for (std::size_t rowIndex = firstIndex; rowIndex < endIndex; ++rowIndex)
			{
				if (!AnchorOf(rows[rowIndex]).has_value())
				{
					continue;
				}

				const bool isInHunk = !IsUnchanged(rows[rowIndex]);
				const std::size_t doubledIndex = rowIndex * 2;
				const std::size_t doubledDistance = doubledIndex > doubledMiddle
														? doubledIndex - doubledMiddle
														: doubledMiddle - doubledIndex;

				const bool isFirstCandidate = !bestIndex.has_value();
				const bool isInHunkOverUnchanged = isInHunk && !bestIsInHunk;
				const bool isNearerOfSameKind =
					isInHunk == bestIsInHunk && doubledDistance < bestDoubledDistance;

				if (isFirstCandidate || isInHunkOverUnchanged || isNearerOfSameKind)
				{
					bestIndex = rowIndex;
					bestIsInHunk = isInHunk;
					bestDoubledDistance = doubledDistance;
				}
			}

			return bestIndex;
		}
	}

	std::optional<std::size_t> RowToKeep(std::span<const DiffRow> rows, std::size_t firstIndex,
										 std::size_t endIndex)
	{
		return FindRowToKeep(rows, firstIndex, endIndex);
	}

	std::optional<std::size_t> RowToKeep(std::span<const DiffPair> pairs, std::size_t firstIndex,
										 std::size_t endIndex)
	{
		return FindRowToKeep(pairs, firstIndex, endIndex);
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

		if (!file.Hunks.empty() && !file.Hunks.back().Lines.empty())
		{
			const Core::DiffLine& lastLine = file.Hunks.back().Lines.back();
			measurement.LastOldLine = lastLine.OldLine;
			measurement.LastNewLine = lastLine.NewLine;
		}

		return measurement;
	}

	std::string_view HunkScope(std::string_view header)
	{
		if (!header.starts_with(kHunkMarker))
		{
			return {};
		}

		const std::size_t closingMarkerIndex = header.find(kHunkMarker, kHunkMarker.size());

		if (closingMarkerIndex == std::string_view::npos)
		{
			return {};
		}

		const std::string_view scope = header.substr(closingMarkerIndex + kHunkMarker.size());

		return Core::WithoutTrailing(Core::WithoutLeading(scope, kIndentation), kTrailingWhitespace);
	}

	std::string LineRangeLabel(int firstLine, int lineCount)
	{
		if (lineCount <= 0)
		{
			return "—";
		}

		if (lineCount == 1)
		{
			return std::to_string(firstLine);
		}

		return std::to_string(firstLine) + "–" + std::to_string(firstLine + lineCount - 1);
	}

	namespace {

		std::string_view WithoutIndentation(std::string_view text)
		{
			const std::size_t firstTextIndex = text.find_first_not_of(kIndentation);
			return firstTextIndex == std::string_view::npos ? std::string_view{}
															: text.substr(firstTextIndex);
		}
	}

	int SignatureLine(std::span<const std::string_view> lines, int beforeLine, std::string_view scope)
	{
		scope = WithoutIndentation(scope);

		if (scope.empty() || beforeLine <= 1)
		{
			return 0;
		}

		const std::size_t searchStart =
			std::min(static_cast<std::size_t>(beforeLine - 1), lines.size());

		for (std::size_t lineNumber = searchStart; lineNumber >= 1; --lineNumber)
		{
			if (WithoutIndentation(lines[lineNumber - 1]).starts_with(scope))
			{
				return static_cast<int>(lineNumber);
			}
		}

		return 0;
	}
}
