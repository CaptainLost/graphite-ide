#include "Git/GitDecorationView.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr std::size_t kGutterAddedSlot = 0;
		constexpr std::size_t kGutterModifiedSlot = 1;
		constexpr std::size_t kGutterRemovedSlot = 2;
	}

	void GitDecorationView::SetWorkingDirectory(const std::filesystem::path& workingDirectory)
	{
		m_WorkingDirectoryKey = Core::PathKey(workingDirectory);
	}

	// WARNING: Goes through PathKey as the index's own keys do: case-folded on Windows, either
	// separator, and "D:\Repos2" is not inside "D:\Repos".
	std::optional<std::filesystem::path> GitDecorationView::RelativeOf(const std::filesystem::path& absolute) const
	{
		return Core::RelativeToKey(absolute, m_WorkingDirectoryKey);
	}

	GitDecoration GitDecorationView::FileOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const std::optional<std::filesystem::path> relative = RelativeOf(absolute);
		return relative ? Index->File(*relative) : GitDecoration::None;
	}

	GitDecoration GitDecorationView::OwnFileOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const std::optional<std::filesystem::path> relative = RelativeOf(absolute);
		return relative ? Index->OwnFile(*relative) : GitDecoration::None;
	}

	GitDecoration GitDecorationView::FolderOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const std::optional<std::filesystem::path> relative = RelativeOf(absolute);
		return relative ? Index->Folder(*relative) : GitDecoration::None;
	}

	bool GitDecorationView::IsUntrackedFolderOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return false;
		}

		const std::optional<std::filesystem::path> relative = RelativeOf(absolute);
		return relative && Index->IsUntrackedFolder(*relative);
	}

	FolderDecoration GitDecorationView::FolderDecorationOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return {};
		}

		const std::optional<std::filesystem::path> relative = RelativeOf(absolute);

		if (!relative)
		{
			return {};
		}

		return {.Decoration = Index->Folder(*relative), .Untracked = Index->IsUntrackedFolder(*relative)};
	}

	ImU32 GitDecorationView::ColourFor(GitDecoration decoration) const
	{
		switch (GitColorOf(decoration))
		{
		case GitColor::GutterAdded:
			return Colours[kGutterAddedSlot];
		case GitColor::GutterRemoved:
			return Colours[kGutterRemovedSlot];
		default:
			break;
		}

		return Colours[kGutterModifiedSlot];
	}

	GitDecoration MaxDecorationOf(std::span<const std::filesystem::path> files, const GitDecorationView& view)
	{
		GitDecoration highest = GitDecoration::None;

		for (const std::filesystem::path& file : files)
		{
			highest = std::max(highest, view.FileOf(file));

			if (highest == GitDecoration::Conflicted)
			{
				break;
			}
		}

		return highest;
	}
}
