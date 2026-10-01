#pragma once

#include "Core/Text/DocumentSet.h"

#include <filesystem>
#include <optional>

namespace Graphite {

	// The path of the active tab, when that tab holds text: empty with no tab active, and empty
	// for a tab that stayed open to say its file could not be read. Every caller that means "the
	// document being worked in" - a command bound to it, the status bar's server slot, the
	// background pass yielding to it - asks this one question, so none of them can forget that
	// an active tab need not be a text document.
	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents);
}
