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
		std::size_t FirstLine = 0;
		std::vector<std::string> Lines;

		// WARNING: Empty for an open document, which the tab's own overlay colours. For a file
		// read from disk it holds the whole file, because a language server colours a file, not
		// an extract of one.
		Core::TextBuffer Text;
		bool TooLarge = false;
		bool Unreadable = false;
	};

	bool IsDrawable(const DefinitionPreview& preview);

	DefinitionPreview LoadDefinitionPreview(const Core::DocumentSet& documents, const std::filesystem::path& path,
											std::size_t line);
}
