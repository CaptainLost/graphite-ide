#pragma once

#include <filesystem>
#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kFolderStateDirectoryName = ".graphite";

	// The folder's own state directory, `.graphite` under its root.
	std::filesystem::path FolderStateDirectory(const std::filesystem::path& root);

	// Creates the folder's state directory, with a .gitignore of `*` inside, so git lists none of it and nothing of
	// Graphite's is ever committed by accident (ADR 0687). False, having logged why, when it cannot be created.
	bool EnsureFolderStateDirectory(const std::filesystem::path& root);
}
