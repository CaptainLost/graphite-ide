#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace Graphite {

	struct FolderDecoration
	{
		GitDecoration Decoration = GitDecoration::None;
		bool Untracked = false;
	};

	class GitDecorationView
	{
	public:
		// WARNING: Null when there is no repository or no status yet. Owned by whoever built the
		// view and valid until it next rebuilds the index.
		const GitDecorations* Index = nullptr;

		// WARNING: Changes whenever the index is rebuilt, so a result computed from one index is
		// not mistaken for the next one's, which may sit at the same address.
		std::uint64_t Version = 0;

		// WARNING: [0] GutterAdded, [1] GutterModified, [2] GutterRemoved, from the active scheme.
		ImU32 Colours[3] = {};

		// WARNING: Without a working directory nothing is decorated.
		void SetWorkingDirectory(const std::filesystem::path& workingDirectory);

		std::optional<std::filesystem::path> RelativeOf(const std::filesystem::path& absolute) const;

		GitDecoration FileOf(const std::filesystem::path& absolute) const;

		GitDecoration OwnFileOf(const std::filesystem::path& absolute) const;

		GitDecoration FolderOf(const std::filesystem::path& absolute) const;
		bool IsUntrackedFolderOf(const std::filesystem::path& absolute) const;

		FolderDecoration FolderDecorationOf(const std::filesystem::path& absolute) const;

		ImU32 ColourFor(GitDecoration decoration) const;

	private:
		Core::PathKeyText m_WorkingDirectoryKey;
	};

	GitDecoration MaxDecorationOf(std::span<const std::filesystem::path> files, const GitDecorationView& view);
}
