#pragma once

#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/ServerCatalog.h"

#include <span>
#include <string>

namespace Graphite {

	std::string ServerOptionLabel(const ServerDefinition& server, bool recommended);

	// Empty for a server that needs no runtime.
	std::string RuntimeLine(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);

	bool RuntimeFound(const ServerRuntime& runtime, std::span<const DotnetInstallation> installations);
}
