#pragma once

#include "Core/Base/Result.h"

#include <filesystem>
#include <string_view>

namespace Graphite {

	// Finds the server binary, in order: `configured` if it names a file that exists; then
	// `command` next to this application's own executable, as a bundled server would sit
	// (see ADR 0488); then `command` on PATH. NotFound when none of the three exists.
	Core::Result<std::filesystem::path> FindServerBinary(std::string_view command,
														 const std::filesystem::path& configured);
}
