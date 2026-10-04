#pragma once

#include "Git/Diff/DiffRows.h"
#include "Git/Diff/FileTexts.h"
#include "Git/Diff/SyntaxRuns.h"

#include "Core/Git/Diff.h"
#include "Core/Text/TextSelection.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Graphite {

	// The line number a selection uses for a row. Unified: the row's ordinal in `rows`.
	// Side-by-side: the file line number on `side`. Absent for headers, signatures and gaps and,
	// side-by-side, for a pair with no row on `side`.
	std::optional<std::int64_t> UnifiedLineOf(std::span<const DiffRow> rows, std::size_t index);
	std::optional<std::int64_t> SideLineOf(const DiffPair& pair, DiffSide side);

	// Text of every selectable line between the selection's ends, in display order, resolved
	// with ResolveRow (unified resolves for New, as the view draws it; side-by-side for `side`).
	std::string SelectedUnifiedText(const Core::TextSelection& selection, std::span<const DiffRow> rows,
									const Core::FileDiff& file,
									std::span<const std::string_view> oldLines,
									std::span<const std::string_view> newLines,
									const FileTokens& tokens);
	std::string SelectedSideText(const Core::TextSelection& selection, DiffSide side,
								 std::span<const DiffPair> pairs, const Core::FileDiff& file,
								 std::span<const std::string_view> oldLines,
								 std::span<const std::string_view> newLines,
								 const FileTokens& tokens);

	// Byte offset in `text` nearest to x (relative to the text's origin), measured as the view
	// draws: width(prefix) for every UTF-8 boundary, binary search, nearest boundary wins.
	std::size_t ByteAtX(std::string_view text, float x,
						const std::function<float(std::string_view prefix)>& widthOf);

	// Byte offset of the character whose drawn extent contains x: the start of that character.
	// Past the end of the text it is text.size(); before its start, zero.
	std::size_t ByteUnderX(std::string_view text, float x,
						   const std::function<float(std::string_view prefix)>& widthOf);

	// Hash of everything a selection's text depends on in the diff: every hunk's start and
	// counts and every line's kind, numbers and text. Computed when the snapshot changes, not
	// per frame; the text functions above likewise run per copy, not per frame.
	std::size_t DiffContentHash(const Core::FileDiff& file);

	// Hash of both sides of the whole-file texts: each side's presence and its text, in order,
	// so equal content in a different object hashes equal and a change to either side does
	// not. Reads every byte, so the caller recomputes it only when the object changes.
	std::size_t TextsContentHash(const FileTexts& texts);

	// What the selection is about, so the view can clear it when that changes. Two contexts
	// compare equal exactly when the selection is still valid.
	struct DiffSelectionContext
	{
		std::filesystem::path Path;
		DiffViewMode Mode = DiffViewMode::SideBySide;
		DiffScope Scope = DiffScope::Hunks;

		// Side-by-side only; New for unified.
		DiffSide Side = DiffSide::New;

		// Hash of the file's hunks text (std::hash over the joined line text).
		std::size_t ContentHash = 0;

		// TextsContentHash of the whole-file texts in whole-file scope, so an edit to an
		// unchanged line invalidates the selection; zero otherwise. Their content rather than
		// the object: a working-tree diff gets a fresh FileTexts on every landing, equal or not.
		std::size_t TextsHash = 0;

		bool operator==(const DiffSelectionContext& other) const = default;
	};
}
