#pragma once

#include <cstdint>
#include <optional>

namespace Graphite {

	// WARNING: Only the terminal writes Exited, ExitCode and Closed. Whoever asked for the command writes only
	// ShowRequested, and the terminal lowers it again when it brings itself forward.
	struct CommandTerminal
	{
		bool Exited = false;
		std::optional<std::uint32_t> ExitCode;
		bool Closed = false;
		bool ShowRequested = false;
	};
}
