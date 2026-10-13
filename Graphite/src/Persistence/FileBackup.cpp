#include "Persistence/FileBackup.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <system_error>

namespace Graphite {

	bool BackUpBeforeWritingOver(const std::filesystem::path& file, std::string_view why)
	{
		std::filesystem::path backup = file;
		backup += ".bak";

		std::error_code error;
		std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);

		if (error)
		{
			return false;
		}

		APP_WARN("'{}' {}; kept it as '{}' before writing over it", Core::ToUtf8(file), why, Core::ToUtf8(backup));
		return true;
	}
}
