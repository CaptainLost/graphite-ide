#include "Git/Changes/ChangesRows.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Graphite {

	namespace {
		bool HasStatusFlag(Core::StatusFlags flags, Core::StatusFlags wanted)
		{
			return (flags & wanted) == wanted;
		}

		bool IsStrictlyUnderDirectory(const std::filesystem::path& path,
									  const std::filesystem::path& directory)
		{
			const std::optional<std::filesystem::path> relativePath = Core::RelativeTo(path, directory);
			return relativePath.has_value() && *relativePath != ".";
		}
	}

	ChangeSections PartitionStatus(const Core::WorkingTreeStatus& status)
	{
		ChangeSections sections;

		for (std::size_t index = 0; index < status.Entries.size(); ++index)
		{
			const Core::FileStatus& file = status.Entries[index];

			if (file.IsConflicted)
			{
				sections.Conflicted.push_back(index);
				continue;
			}

			if (file.Index != Core::StatusFlags::None)
			{
				sections.Staged.push_back(index);
			}

			if (file.WorkTree != Core::StatusFlags::None)
			{
				sections.Unstaged.push_back(index);
			}
		}

		return sections;
	}

	char ChangeLetter(Core::StatusFlags flags, bool untracked)
	{
		if (untracked)
		{
			return 'U';
		}

		if (HasStatusFlag(flags, Core::StatusFlags::Deleted))
		{
			return 'D';
		}

		if (HasStatusFlag(flags, Core::StatusFlags::Renamed))
		{
			return 'R';
		}

		if (HasStatusFlag(flags, Core::StatusFlags::New))
		{
			return 'A';
		}

		if (HasStatusFlag(flags, Core::StatusFlags::TypeChanged))
		{
			return 'T';
		}

		if (HasStatusFlag(flags, Core::StatusFlags::Modified))
		{
			return 'M';
		}

		return ' ';
	}

	std::optional<std::filesystem::path> NextSelection(
		const std::filesystem::path& selected,
		const std::vector<std::filesystem::path>& before,
		const std::vector<std::filesystem::path>& after)
	{
		if (std::find(after.begin(), after.end(), selected) != after.end())
		{
			return selected;
		}

		if (after.empty())
		{
			return std::nullopt;
		}

		const std::vector<std::filesystem::path>::const_iterator selectedInBefore =
			std::find(before.begin(), before.end(), selected);

		if (selectedInBefore == before.end())
		{
			return std::nullopt;
		}

		const std::size_t position = static_cast<std::size_t>(selectedInBefore - before.begin());

		return after[std::min(position, after.size() - 1)];
	}

	DiscardPlan PlanDiscard(const DiscardSubject& subject,
							const std::filesystem::path& repositoryWorkingDirectory,
							const std::vector<std::filesystem::path>& unsavedDocumentPaths)
	{
		DiscardPlan plan;

		if (subject.IsUntracked)
		{
			plan.Delete.push_back(subject.Path);
		}
		else
		{
			plan.Restore.push_back(subject.Path);
		}

		if (subject.IsDirectory)
		{
			if (subject.Listing.has_value())
			{
				plan.Files = *subject.Listing;
				plan.FileCount = plan.Files.size();
			}
		}
		else
		{
			plan.Files.push_back(subject.Path);
		}

		const std::filesystem::path absoluteSubject =
			Core::NormalizePath(repositoryWorkingDirectory / subject.Path);

		for (const std::filesystem::path& document : unsavedDocumentPaths)
		{
			const std::filesystem::path normalisedDocument = Core::NormalizePath(document);

			const bool isTheSubject = Core::SamePath(normalisedDocument, absoluteSubject);
			const bool isUnderTheSubject = subject.IsDirectory && IsStrictlyUnderDirectory(normalisedDocument, absoluteSubject);

			if (isTheSubject || isUnderTheSubject)
			{
				plan.Dirty.push_back(document);
			}
		}

		return plan;
	}

	std::string DiscardQuestionText(const DiscardSubject& subject, const DiscardPlan& plan)
	{
		const std::string name = Core::ToUtf8(subject.Path);

		if (subject.IsDirectory)
		{
			if (plan.FileCount.has_value())
			{
				return std::format("Delete {}/ and the {} files in it?", name, *plan.FileCount);
			}

			return std::format("Delete {}/ and everything in it?", name);
		}

		if (!plan.Delete.empty())
		{
			return std::format("Delete {}?", name);
		}

		// WARNING: A discard restores from the index (Repository::DiscardWorkTree), so only the
		// unstaged edits are lost, not everything since the last commit.
		return std::format("Discard the unstaged changes to {}?", name);
	}

	RowText FitRowText(float available, float nameWidth, float directoryWidth, float gapWidth,
					   float minDirectoryWidth)
	{
		if (available <= 0.0f)
		{
			return {};
		}

		RowText text;
		text.Name = std::min(nameWidth, available);

		const float roomForDirectory = available - text.Name - gapWidth;

		if (directoryWidth > 0.0f && roomForDirectory >= std::min(minDirectoryWidth, directoryWidth))
		{
			text.Directory = std::min(directoryWidth, roomForDirectory);
		}

		return text;
	}

	FolderRowPlacement PlaceFolderRow(FolderRowGlyphs closedGlyphs, FolderRowGlyphs drawnGlyphs,
									  float fileRowIconInkToNameGap, float nameSideBearing)
	{
		const float fileRowVisibleGap = fileRowIconInkToNameGap + nameSideBearing;

		const float closedChevronInkWidth =
			closedGlyphs.Chevron.EndFromDrawX - closedGlyphs.Chevron.StartFromDrawX;
		const float nameOffset = closedGlyphs.Folder.EndFromDrawX + fileRowVisibleGap +
								 closedChevronInkWidth + fileRowIconInkToNameGap;

		const float nameInkOffset = nameOffset + nameSideBearing;
		const float drawnChevronInkWidth =
			drawnGlyphs.Chevron.EndFromDrawX - drawnGlyphs.Chevron.StartFromDrawX;
		const float gapEitherSideOfChevron =
			(nameInkOffset - drawnGlyphs.Folder.EndFromDrawX - drawnChevronInkWidth) / 2.0f;
		const float chevronOffset =
			drawnGlyphs.Folder.EndFromDrawX + gapEitherSideOfChevron - drawnGlyphs.Chevron.StartFromDrawX;

		return FolderRowPlacement{.ChevronOffsetFromIcon = std::round(chevronOffset),
								  .NameOffsetFromIcon = std::round(nameOffset)};
	}

	float FolderChildIndent(const FolderRowPlacement& placement, Core::GlyphInk drawnChevron,
							float iconColumnWidth)
	{
		const float chevronInkCentre = placement.ChevronOffsetFromIcon +
									   (drawnChevron.StartFromDrawX + drawnChevron.EndFromDrawX) / 2.0f;

		return std::round(chevronInkCentre - iconColumnWidth / 2.0f);
	}
}
