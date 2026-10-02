#include "Settings/SettingsBackup.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

#include <system_error>

namespace Graphite {

	bool BackUpMalformedSettings(const std::filesystem::path& file)
	{
		std::filesystem::path backup = file;
		backup += ".bak";

		std::error_code error;
		std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);

		if (error)
		{
			return false;
		}

		APP_WARN("'{}' does not parse; kept it as '{}' before writing over it", Core::ToUtf8(file),
				 Core::ToUtf8(backup));
		return true;
	}
}
