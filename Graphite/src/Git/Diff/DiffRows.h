#pragma once

#include "Git/Diff/DiffViewOptions.h"
#include "Git/Diff/SyntaxRuns.h"

#include "Core/Git/Diff.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	enum class DiffRowKind
	{
		HunkHeader,
		HunkSignature,
		HunkGap,
		Context,
		Added,
		Removed,
		Unchanged
	};

	// WARNING: A row carries indices, not text, and is valid only against the FileDiff it was
	// built from. A stale row can still be in range of a newer diff and resolve to the wrong
	// line instead of to nothing. An Unchanged row belongs to no hunk: its Hunk and Line stay
	// zero and must not be read.
	struct DiffRow
	{
		DiffRowKind Kind = DiffRowKind::Context;
		int OldLine = 0;
		int NewLine = 0;
		std::size_t Hunk = 0;
		std::size_t Line = 0;
	};

	struct DiffPair
	{
		std::optional<DiffRow> Left;
		std::optional<DiffRow> Right;
	};

	std::vector<DiffRow> BuildUnifiedRows(const Core::FileDiff& file);

	std::vector<DiffPair> BuildSideBySideRows(const Core::FileDiff& file);

	const Core::DiffLine* DiffLineOf(const Core::FileDiff& file, const DiffRow& row);

	struct WholeFileRows
	{
		std::vector<DiffRow> Unified;
		std::vector<DiffPair> SideBySide;
	};

	WholeFileRows BuildWholeFileRows(const Core::FileDiff& file, std::size_t oldLineCount,
									 std::size_t newLineCount);

	enum class DiffSide
	{
		Old,
		New
	};

	struct ResolvedLine
	{
		std::optional<std::string_view> Text;
		const std::vector<Token>* Tokens = nullptr;
	};

	ResolvedLine ResolveRow(const Core::FileDiff& file, const DiffRow& row, DiffSide side,
							std::span<const std::string_view> oldLines,
							std::span<const std::string_view> newLines,
							const FileTokens& tokens);

	struct DiffLineAnchor
	{
		DiffSide Side = DiffSide::New;
		int Line = 0;

		bool operator==(const DiffLineAnchor& other) const = default;
	};

	std::optional<DiffLineAnchor> AnchorOf(const DiffRow& row);

	std::optional<DiffLineAnchor> AnchorOf(const DiffPair& pair);

	struct DiffRowAt
	{
		std::size_t Row = 0;
		bool Exact = false;

		bool operator==(const DiffRowAt& other) const = default;
	};

	DiffRowAt RowAt(std::span<const DiffRow> rows, DiffLineAnchor anchor);
	DiffRowAt RowAt(std::span<const DiffPair> pairs, DiffLineAnchor anchor);

	std::optional<std::size_t> RowToKeep(std::span<const DiffRow> rows, std::size_t firstIndex,
										 std::size_t endIndex);
	std::optional<std::size_t> RowToKeep(std::span<const DiffPair> pairs, std::size_t firstIndex,
										 std::size_t endIndex);

	// WARNING: The path alone does not tell two diffs apart. One DiffView serves every file of
	// every commit, so the same path in the next commit has different lines, and a width kept
	// from the previous diff would leave the reader scrolled into blank space.
	struct DiffMeasurement
	{
		std::filesystem::path Path;
		DiffScope Scope = DiffScope::Hunks;
		std::size_t Hunks = 0;
		std::size_t Lines = 0;
		int LastOldLine = 0;
		int LastNewLine = 0;

		bool operator==(const DiffMeasurement& other) const = default;
	};

	DiffMeasurement MeasurementOf(const Core::FileDiff& file, DiffScope scope);

	std::string_view HunkScope(std::string_view header);

	int SignatureLine(std::span<const std::string_view> lines, int beforeLine, std::string_view scope);

	std::string LineRangeLabel(int firstLine, int lineCount);
}
