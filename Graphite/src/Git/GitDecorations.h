#pragma once

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Status.h"
#include "Theme/GitColor.h"

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace Graphite {

	// WARNING: Ordered by priority: a higher value wins when a file or folder carries several.
	enum class GitDecoration : std::uint8_t
	{
		None,
		Untracked,
		Added,
		Renamed,
		Modified,
		Conflicted
	};

	char LetterOf(GitDecoration decoration);

	GitColor GitColorOf(GitDecoration decoration);

	GitDecoration DecorationOf(const Core::FileStatus& entry);

	GitDecoration FolderWeightOf(const Core::FileStatus& entry);

	class GitDecorations
	{
	public:
		GitDecorations() = default;
		explicit GitDecorations(const Core::WorkingTreeStatus& status);

		GitDecoration File(const std::filesystem::path& relative) const;

		GitDecoration OwnFile(const std::filesystem::path& relative) const;

		GitDecoration Folder(const std::filesystem::path& relative) const;

		bool IsUntrackedFolder(const std::filesystem::path& relative) const;

		// Whether the path is a checkout nested in the working directory, or lies under one. The repository does not
		// answer for it, so it carries no decoration here. Decided from the status alone: no disk access.
		bool IsInNestedCheckout(const std::filesystem::path& relative) const;

		bool IsEmpty() const;

	private:
		std::unordered_map<Core::PathKeyText, GitDecoration> m_Files;
		std::unordered_map<Core::PathKeyText, GitDecoration> m_Folders;
		std::unordered_set<Core::PathKeyText> m_UntrackedFolders;
		std::unordered_set<Core::PathKeyText> m_NestedCheckouts;
	};
}
