#include "Persistence/FolderStateDirectory.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include <system_error>

namespace Graphite {

	namespace {

		constexpr std::string_view kIgnoreEverything = "# Graphite's state for this folder; never committed.\n*\n";
	}

	std::filesystem::path FolderStateDirectory(const std::filesystem::path& root)
	{
		return root / kFolderStateDirectoryName;
	}

	bool EnsureFolderStateDirectory(const std::filesystem::path& root)
	{
		const std::filesystem::path directory = FolderStateDirectory(root);

		std::error_code createError;
		std::filesystem::create_directories(directory, createError);

		if (createError)
		{
			APP_WARN("Cannot create '{}': {}", Core::ToUtf8(directory), createError.message());
			return false;
		}

		// WARNING: Written only when missing, so a reader who chose to commit some of it and edited the file keeps their
		// edit; a directory made by an older build gets one at its next write.
		const std::filesystem::path ignoreFile = directory / ".gitignore";
		std::error_code existsError;

		if (!std::filesystem::exists(ignoreFile, existsError) && !existsError)
		{
			if (const Core::Result<void> written = Core::WriteFileAtomically(ignoreFile, kIgnoreEverything); !written)
			{
				APP_WARN("Cannot write '{}': {}", Core::ToUtf8(ignoreFile), written.GetError().Message);
			}
		}

		return true;
	}
}
