#pragma once

#include "Lang/Api/LanguageService.h"

#include <cstddef>
#include <filesystem>
#include <functional>

namespace Core {

	class DocumentSet;
}

namespace Graphite {

	// Opens `path` the way the caller's context requires. Passed in rather than chosen here
	// because the two kinds of caller genuinely differ:
	//
	//  - A panel that holds the Workspace opens through Workspace::RequestOpenFile. That is the
	//    full path: DocumentWindows::RequestOpen also asks the large-file question when
	//    DocumentSet::Open answers NeedsConfirmation, and clears the carried-over mark that
	//    keeps a tab out of the new folder's saved state. Calling DocumentSet::Open directly
	//    skips both, so a big file would simply never open.
	//  - A panel that lists only documents already open (ProblemsPanel) has no Workspace and
	//    nothing to confirm: DocumentSet::Open on an open path only activates its tab.
	//
	// Either way a click is somewhere the user meant to go, so callers open permanently rather
	// than as a preview tab.
	using OpenDocument = std::function<void(const std::filesystem::path&)>;

	// Whether a range that starts at (line, column) and ends at (endLine, endColumn) selects text
	// on its starting line. A range that ends on another line is not one the single-line
	// selection can honour, and an empty one is a point.
	bool SpansOneLine(std::size_t line, Utf16Column column, std::size_t endLine,
					  Utf16Column endColumn);

	// Opens `path` and selects [from, to) bytes of line `line`, scrolling the editor there.
	// Returns false when there is nothing to select in: the open did not produce a document
	// (a large file still waiting on its confirmation, a failed read).
	//
	// The range comes from a reply that may be some frames old and the file may have changed
	// since, so offsets are clamped by the caret placement to the buffer's current length rather
	// than trusted; a line past the end of a shrunk document lands on its last line.
	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, std::size_t from, std::size_t to,
						  const OpenDocument& open);

	// The same for a range in the protocol's own units: UTF-16 columns, with the end on the
	// starting line or elsewhere. The columns are converted against the line's text as the
	// buffer holds it now. A range that does not stay on its line selects only the point it
	// starts at.
	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, Utf16Column column, std::size_t endLine,
						  Utf16Column endColumn, const OpenDocument& open);
}
