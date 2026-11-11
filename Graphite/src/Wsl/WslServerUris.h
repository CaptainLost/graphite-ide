#pragma once

#include "Core/FileSystem/WslSharePath.h"

#include <string>
#include <string_view>

namespace Graphite {

	// A language server running in a WSL distro names files by Linux paths; Graphite names them by
	// their share paths (ADR 0696). These rewrite one JSON-RPC body between the two spellings:
	// every string that is a file URI of that distro, and the initialize request's rootPath. A body
	// holding neither is returned unchanged, without being parsed; so is one that is not JSON.

	// Host to server: file:////wsl.localhost/<distro>/<path> becomes file:///<path>.
	std::string ToDistroBody(std::string_view body, const Core::WslSharePath& root);

	// Server to host: file:///<path> becomes the share URI of <path> in distro.
	std::string FromDistroBody(std::string_view body, const std::string& distro);
}
