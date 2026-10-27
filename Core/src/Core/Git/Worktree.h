#pragma once

#include "Core/Git/Head.h"

#include <filesystem>
#include <string>

namespace Core {

	struct WorktreeEntry
	{
		std::filesystem::path Path;

		// git's name for a linked worktree; empty for the main checkout.
		std::string Name;

		bool IsMain = false;

		// Read from that worktree. Unread (default) when FolderMissing.
		HeadState Head;

		bool Locked = false;
		std::string LockReason;

		// The folder is gone or no longer opens as that worktree.
		bool FolderMissing = false;
	};
}
