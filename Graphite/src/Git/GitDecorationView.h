#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace Graphite {

	// A folder's own lookup: the highest decoration beneath it, and whether it is, or lies inside, a
	// directory git reports as untracked.
	struct FolderDecoration
	{
		GitDecoration Decoration = GitDecoration::None;
		bool Untracked = false;
	};

	// What a tree needs to decorate its rows for one frame: the index of the current status
	// snapshot, the directory its paths are relative to, and the three colours it draws in. Rows
	// hold absolute paths; this turns them into the index's keys. Cheap to copy: the working
	// directory is held as its key, made once.
	class GitDecorationView
	{
	public:
		// Null when there is no repository or no status yet; nothing is decorated then. Owned by
		// whoever built the view and valid until it next rebuilds the index.
		const GitDecorations* Index = nullptr;

		// Changes whenever the index is rebuilt, so a result computed from one index is not mistaken
		// for the next one's, which may sit at the same address.
		std::uint64_t Version = 0;

		// [0] GutterAdded, [1] GutterModified, [2] GutterRemoved, from the active scheme.
		ImU32 Colours[3] = {};

		// GitModel::GetWorkingDirectory(), which is not always the workspace root. Without one
		// nothing is decorated.
		void SetWorkingDirectory(const std::filesystem::path& workingDirectory);

		// `absolute` spelled relative to the working directory, "." for the directory itself;
		// nothing when it lies outside, or when there is no working directory.
		std::optional<std::filesystem::path> RelativeOf(const std::filesystem::path& absolute) const;

		GitDecoration FileOf(const std::filesystem::path& absolute) const;

		// FileOf without what a file inherits from an untracked directory around it.
		GitDecoration OwnFileOf(const std::filesystem::path& absolute) const;

		GitDecoration FolderOf(const std::filesystem::path& absolute) const;
		bool IsUntrackedFolderOf(const std::filesystem::path& absolute) const;

		// FolderOf and IsUntrackedFolderOf with the path keyed once.
		FolderDecoration FolderDecorationOf(const std::filesystem::path& absolute) const;

		ImU32 ColourFor(GitDecoration decoration) const;

	private:
		std::filesystem::path::string_type m_WorkingDirectoryKey;
	};

	// The highest decoration among `files`, None for no files or nothing decorated.
	GitDecoration MaxDecorationOf(std::span<const std::filesystem::path> files, const GitDecorationView& view);
}
