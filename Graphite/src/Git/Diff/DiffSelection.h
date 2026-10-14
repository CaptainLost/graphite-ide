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

	std::optional<std::int64_t> UnifiedLineOf(std::span<const DiffRow> rows, std::size_t index);
	std::optional<std::int64_t> SideLineOf(const DiffPair& pair, DiffSide side);

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

	std::size_t ByteAtX(std::string_view text, float x,
						const std::function<float(std::string_view prefix)>& widthOf);

	std::size_t ByteUnderX(std::string_view text, float x,
						   const std::function<float(std::string_view prefix)>& widthOf);

	// WARNING: Reads every hunk and line. Compute it when the snapshot changes, not per frame.
	std::size_t DiffContentHash(const Core::FileDiff& file);

	// WARNING: Reads every byte of both texts. Recompute it only when the texts object changes.
	std::size_t TextsContentHash(const FileTexts& texts);

	struct DiffSelectionContext
	{
		std::filesystem::path Path;
		DiffViewMode Mode = DiffViewMode::SideBySide;
		DiffScope Scope = DiffScope::Hunks;
		DiffSide Side = DiffSide::New;
		std::size_t ContentHash = 0;
		std::size_t TextsHash = 0;

		bool operator==(const DiffSelectionContext& other) const = default;
	};
}
