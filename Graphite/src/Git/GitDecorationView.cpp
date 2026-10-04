#include "Git/GitDecorationView.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	void GitDecorationView::SetWorkingDirectory(const std::filesystem::path& workingDirectory)
	{
		m_WorkingDirectoryKey = Core::PathKey(workingDirectory);
	}

	std::optional<std::filesystem::path> GitDecorationView::RelativeOf(const std::filesystem::path& absolute) const
	{
		// Through PathKey, as the index's own keys are: case-folded on Windows, either separator,
		// and "D:\Repos2" is not inside "D:\Repos".
		return Core::RelativeToKey(absolute, m_WorkingDirectoryKey);
	}

	GitDecoration GitDecorationView::FileOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const auto relative = RelativeOf(absolute);
		return relative ? Index->File(*relative) : GitDecoration::None;
	}

	GitDecoration GitDecorationView::OwnFileOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const auto relative = RelativeOf(absolute);
		return relative ? Index->OwnFile(*relative) : GitDecoration::None;
	}

	GitDecoration GitDecorationView::FolderOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return GitDecoration::None;
		}

		const auto relative = RelativeOf(absolute);
		return relative ? Index->Folder(*relative) : GitDecoration::None;
	}

	bool GitDecorationView::IsUntrackedFolderOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return false;
		}

		const auto relative = RelativeOf(absolute);
		return relative && Index->IsUntrackedFolder(*relative);
	}

	FolderDecoration GitDecorationView::FolderDecorationOf(const std::filesystem::path& absolute) const
	{
		if (Index == nullptr || Index->IsEmpty())
		{
			return {};
		}

		const auto relative = RelativeOf(absolute);

		if (!relative)
		{
			return {};
		}

		return {.Decoration = Index->Folder(*relative), .Untracked = Index->IsUntrackedFolder(*relative)};
	}

	ImU32 GitDecorationView::ColourFor(GitDecoration decoration) const
	{
		switch (ColourOf(decoration))
		{
		case GitColor::GutterAdded: return Colours[0];
		case GitColor::GutterRemoved: return Colours[2];
		default: break;
		}

		return Colours[1];
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
