#pragma once

#include "Git/GitDecorations.h"

#include <imgui.h>

#include <filesystem>
#include <optional>

namespace Graphite {

	// What a tree needs to decorate its rows for one frame: the index of the current status
	// snapshot, the directory its paths are relative to, and the three colours it draws in. Rows
	// hold absolute paths; this turns them into the index's keys.
	struct GitDecorationView
	{
		// Null when there is no repository or no status yet; nothing is decorated then. Owned by
		// whoever built the view and valid until it next rebuilds the index.
		const GitDecorations* Index = nullptr;

		// GitModel::GetWorkingDirectory(), which is not always the workspace root.
		std::filesystem::path WorkingDirectory;

		// [0] GutterAdded, [1] GutterModified, [2] GutterRemoved, from the active scheme.
		ImU32 Colours[3] = {};

		// `absolute` spelled relative to WorkingDirectory, "." for the directory itself; nothing
		// when it lies outside, or when there is no working directory.
		std::optional<std::filesystem::path> RelativeOf(const std::filesystem::path& absolute) const;

		GitDecoration FileOf(const std::filesystem::path& absolute) const;
		GitDecoration FolderOf(const std::filesystem::path& absolute) const;
		bool IsUntrackedFolderOf(const std::filesystem::path& absolute) const;

		ImU32 ColourFor(GitDecoration decoration) const;
	};
}
