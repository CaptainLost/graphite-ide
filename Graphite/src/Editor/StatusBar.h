#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	// Bundled rather than two adjacent std::optional<std::string> parameters: a caller could
	// transpose those silently, which is exactly the hazard ADR 0035 exists to prevent.
	struct StatusMessages
	{
		std::optional<std::string> Transient;
		std::optional<std::string> Search;
	};

	void DrawStatusBar(const Core::DocumentSet& documents,
					   const std::filesystem::path& root,
					   const StatusMessages& messages);

}
