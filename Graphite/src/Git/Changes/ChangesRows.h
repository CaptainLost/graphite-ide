#pragma once

#include "Core/Git/Status.h"
#include "Core/ImGui/TextMetrics.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct ChangeSections
	{
		std::vector<std::size_t> Conflicted;
		std::vector<std::size_t> Staged;
		std::vector<std::size_t> Unstaged;
	};

	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status);

	char ChangeLetter(Core::StatusFlags flags, bool untracked);

	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after);

	struct DiscardSubject
	{
		std::filesystem::path Path;

		bool IsDirectory = false;
		bool IsUntracked = false;

		std::optional<std::vector<std::filesystem::path>> Listing;
	};

	struct DiscardPlan
	{
		std::vector<std::filesystem::path> Restore;
		std::vector<std::filesystem::path> Delete;

		std::vector<std::filesystem::path> Files;

		std::optional<std::size_t> FileCount;

		std::vector<std::filesystem::path> Dirty;
	};

	DiscardPlan PlanDiscard(const DiscardSubject& subject,
							const std::filesystem::path& repositoryWorkingDirectory,
							const std::vector<std::filesystem::path>& unsavedDocumentPaths);

	std::string DiscardQuestionText(const DiscardSubject& subject, const DiscardPlan& plan);

	struct RowText
	{
		float Name = 0.0f;
		float Directory = 0.0f;
	};

	RowText FitRowText(float available, float nameWidth, float directoryWidth, float gapWidth,
					   float minDirectoryWidth);

	struct FolderRowGlyphs
	{
		Core::GlyphInk Folder;
		Core::GlyphInk Chevron;
	};

	struct FolderRowPlacement
	{
		float ChevronOffsetFromIcon = 0.0f;
		float NameOffsetFromIcon = 0.0f;
	};

	FolderRowPlacement PlaceFolderRow(FolderRowGlyphs closedGlyphs, FolderRowGlyphs drawnGlyphs,
									  float fileRowIconInkToNameGap, float nameSideBearing);

	float FolderChildIndent(const FolderRowPlacement& placement, Core::GlyphInk drawnChevron,
							float iconColumnWidth);
}
