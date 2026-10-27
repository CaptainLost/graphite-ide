#pragma once

#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <filesystem>
#include <functional>

namespace Core {

	class DocumentSet;
	class TextBuffer;
}

namespace Graphite {

	// WARNING: A panel with a Workspace must open through Workspace::RequestOpenFile. Calling
	// DocumentSet::Open directly skips the large-file confirmation, so a big file never opens.
	using OpenDocument = std::function<void(const std::filesystem::path&)>;

	bool SpansOneLine(std::size_t line, Utf16Column column, std::size_t endLine,
					  Utf16Column endColumn);

	// WARNING: Reads only the prefix the column can reach, so a giant line is not copied for it.
	std::size_t ByteColumnOfUtf16Column(const Core::TextBuffer& buffer, std::size_t line, Utf16Column column);

	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, std::size_t from, std::size_t to,
						  const OpenDocument& open);

	bool SelectInDocument(Core::DocumentSet& documents, const std::filesystem::path& path,
						  std::size_t line, Utf16Column column, std::size_t endLine,
						  Utf16Column endColumn, const OpenDocument& open);
}
