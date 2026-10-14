#pragma once

#include "Language/Install/Checksum.h"
#include "Language/Server/ServerPackage.h"

#include <optional>
#include <string>

namespace Graphite {

	struct ResolvedPackage
	{
		std::string Version;
		std::string DownloadUrl;
		std::string FileName;
		std::optional<Checksum> ExpectedChecksum;
		ArchiveKind Archive = ArchiveKind::Zip;

		// WARNING: Empty for a NuGet tool, whose command is known only once its DotnetToolSettings.xml is read.
		std::string ExecutableInArchive;
	};
}
