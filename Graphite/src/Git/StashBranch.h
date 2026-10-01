#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The branch a stash was made on, read from the prefix git writes into its message:
	// "On <branch>: <message>" or "WIP on <branch>: <commit summary>". The branch runs to the
	// first colon, which a branch name cannot contain. Nothing for a message without that
	// prefix - one written by a tool that sets its own.
	std::optional<std::string> StashBranch(std::string_view message);
}
