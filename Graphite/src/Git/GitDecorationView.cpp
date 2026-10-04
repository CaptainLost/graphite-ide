#include "Git/GitDecorationView.h"

#include "Core/FileSystem/FileSystem.h"

namespace Graphite {

	std::optional<std::filesystem::path> GitDecorationView::RelativeOf(const std::filesystem::path& absolute) const
	{
		// Through PathKey, as the index's own keys are: case-folded on Windows, either separator,
		// and "D:\Repos2" is not inside "D:\Repos".
		return Core::RelativeTo(absolute, WorkingDirectory);
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
}
