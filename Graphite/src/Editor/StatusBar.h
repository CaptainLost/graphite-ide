#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	void DrawStatusBar(const Core::DocumentSet& documents,
					   const std::filesystem::path& root,
					   const std::optional<std::string>& transientMessage);

}
