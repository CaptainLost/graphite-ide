#pragma once

#include "Core/Text/DocumentSet.h"

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
		bool TooLarge = false;                // the header says "file too large to preview"
		bool Unreadable = false;              // the file could not be read; nothing to show
	};

	// The open document at `path` if there is one, else the file on disk (under the size limit,
	// decoded as the editor decodes), cut to the definition's extent from `line`.
	DefinitionPreview LoadDefinitionPreview(const Core::DocumentSet& documents,
											const std::filesystem::path& path, std::size_t line);
}
