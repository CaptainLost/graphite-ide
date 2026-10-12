#pragma once

#include "Core/Text/DocumentSet.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

namespace Graphite {

	// The path of the active tab, when that tab holds text: empty with no tab active, and empty
	// for a tab that stayed open to say its file could not be read. Every caller that means "the
	// document being worked in" - a command bound to it, the status bar's server slot, the
	// background pass yielding to it - asks this one question, so none of them can forget that
	// an active tab need not be a text document.
	std::optional<std::filesystem::path> ActiveTextDocument(const Core::DocumentSet& documents);

	// The active text document's index, but only while its tab is the one on screen. A document
	// stays active when focus moves to a panel or another tab covers it (ADR 0547), which is right
	// for commands and wrong for anything describing what the reader is looking at: with the Git
	// view in front of it, the status bar went on reporting the file's caret and encoding.
	std::optional<std::size_t> ShownActiveDocument(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown);

	// Text or not: a tab that stayed open to say its file could not be read is still that file
	// in front of the reader.
	std::optional<std::size_t> ShownActiveTab(
		const Core::DocumentSet& documents,
		const std::function<bool(const std::filesystem::path&)>& isShown);
}
