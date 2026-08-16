#pragma once

#include "Lang/Api/LanguageService.h"

#include <string>

namespace Graphite {

	// The status bar's short phrase for a server's state, e.g. "clangd: indexing 42%".
	//
	// Empty when the language has no server configured — "no server" over a plain text file is
	// noise, not information, so the bar shows nothing at all for it rather than training the
	// eye to skim past a slot that always has something in it.
	//
	// ImGui-free by design, alongside ServerStatusDetail below: StatusBar.cpp includes ImGui and
	// GraphiteTests does not link it, so the string-building has to live somewhere the test
	// project can compile without ImGui at all. StatusBar.cpp calls into these rather than
	// building the strings itself.
	std::string ServerStatusText(const ServerStatus& status);

	// The longer explanation behind ServerStatusText, shown as a tooltip on hover. Says what is
	// missing rather than restating the state: for NotFound, where the binary was searched for;
	// for NoCompileDatabase, what file would fix it and what the server can see without one; for
	// a running server, the binary path and the version its handshake reported. Notes a restart
	// count once there has been one, regardless of state.
	std::string ServerStatusDetail(const ServerStatus& status);

}
