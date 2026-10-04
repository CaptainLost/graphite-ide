#pragma once

#include "Core/Git/Status.h"
#include "Theme/GitColor.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace Graphite {

	// Ordered by priority: a higher value wins when a file or folder carries several.
	enum class GitDecoration : std::uint8_t
	{
		None,
		Untracked,
		Added,
		Renamed,
		Modified,
		Conflicted
	};

	// '\0' for None, otherwise 'U', 'A', 'R', 'M' or '!'.
	char LetterOf(GitDecoration decoration);

	// Reuses the scheme's gutter slots. None gives GutterModified, which nothing draws.
	GitColor ColourOf(GitDecoration decoration);

	// What a status entry shows on its own row. The tree ignores the staged/unstaged split, so
	// one value covers both halves. A deleted file has no row to decorate and gives None; see
	// FolderWeightOf.
	GitDecoration DecorationOf(const Core::FileStatus& entry);

	// What an entry contributes to the folders above it: its own decoration, or Modified for a
	// deleted file, so a folder still shows that something under it changed.
	GitDecoration FolderWeightOf(const Core::FileStatus& entry);

	// One status per path, built once from a status snapshot. Lookups are by key, never a scan.
	class GitDecorations
	{
	public:
		GitDecorations() = default;
		explicit GitDecorations(const Core::WorkingTreeStatus& status);

		// `relative` is relative to the repository's working directory, spelled with either
		// separator and in any case Windows would accept. An untracked directory is reported by
		// git as one entry, so everything beneath it is Untracked here.
		GitDecoration File(const std::filesystem::path& relative) const;

		// The highest decoration among the folder's descendants; None when clean. The empty path
		// is the working directory itself.
		GitDecoration Folder(const std::filesystem::path& relative) const;

		// Whether `relative` is, or lies inside, a directory git reported as untracked.
		bool IsUntrackedFolder(const std::filesystem::path& relative) const;

		bool IsEmpty() const;

	private:
		using Key = std::filesystem::path::string_type;

		std::unordered_map<Key, GitDecoration> m_Files;
		std::unordered_map<Key, GitDecoration> m_Folders;
		std::unordered_set<Key> m_UntrackedFolders;
	};
}
