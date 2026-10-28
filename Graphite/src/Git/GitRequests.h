#pragma once

#include "Core/Git/Oid.h"

#include <filesystem>

namespace Graphite {

	// Asks for a file's history in the Git panel.
	struct ShowFileHistoryRequested
	{
		std::filesystem::path RelativePath;   // relative to the git working directory
	};

	// Asks for a commit in the Git panel, opened at a file it changed.
	struct ShowCommitRequested
	{
		Core::Oid Commit;
		std::filesystem::path RelativePath;   // relative to the git working directory, as the file was in Commit
	};
}
