#pragma once

#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Graphite {

	inline constexpr std::uintmax_t kPreviewFileByteLimit = 2u * 1024u * 1024u;

	struct DefinitionPreview
	{
		std::filesystem::path Path;
		std::size_t FirstLine = 0;            // 0-based; the header shows FirstLine + 1
		std::vector<std::string> Lines;       // raw lines, tabs not expanded

		// The whole file the lines were cut from, for a file read from disk. A language server
		// colours a file, not an extract of one, and its answer is measured against this text.
		// Empty for an open document, which the tab's own overlay colours.
		Core::TextBuffer Text;
		bool TooLarge = false;                // the header says "file too large to preview"
		bool Unreadable = false;              // the file could not be read; nothing to show
	};

	// The open document at `path` if there is one, else the file on disk (under the size limit,
	// decoded as the editor decodes), cut to the definition's extent from `line`.
	// Whether the tooltip has a preview part to draw. A file too large keeps its header; a
	// definition that could not be read, or a line past the file's end, leaves nothing to say —
	// the identifier simply has no definition to show, and a box saying so is noise.
	bool IsDrawable(const DefinitionPreview& preview);

	DefinitionPreview LoadDefinitionPreview(const Core::DocumentSet& documents,
											const std::filesystem::path& path, std::size_t line);
}
