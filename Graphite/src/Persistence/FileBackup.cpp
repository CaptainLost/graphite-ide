#include "Persistence/FileBackup.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"

#include <system_error>

namespace Graphite {

	bool BackUpBeforeWritingOver(const std::filesystem::path& file, std::string_view reason)
	{
		std::filesystem::path backup = file;
		backup += ".bak";

		std::error_code copyError;
		std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, copyError);

		if (copyError)
		{
			return false;
		}

		APP_WARN("'{}' {}; kept it as '{}' before writing over it", Core::ToUtf8(file), reason, Core::ToUtf8(backup));
		return true;
	}
}
