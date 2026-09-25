#pragma once

#include "Core/Platform/WindowPlacement.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// What outlives the application whichever folder is open.
	struct GlobalState
	{
		std::optional<Core::WindowPlacement> Window;

		// Only panels whose visibility differs from their default, keyed by panel id. A panel
		// added in a later version then starts the way its author meant, rather than the way a
		// list written before it existed happens to say.
		std::map<std::string, bool> PanelVisibility;

		friend bool operator==(const GlobalState&, const GlobalState&) = default;
	};

	// One open file document, as the next launch reopens it.
	struct TabState
	{
		std::filesystem::path Path;
		bool Preview = false;

		// The caret, as a line and a byte offset into that line. A line survives the file being
		// edited elsewhere far better than a document offset does.
		std::size_t Line = 0;
		std::size_t Column = 0;

		// The document line at the top of the view.
		std::size_t Top = 0;

		friend bool operator==(const TabState&, const TabState&) = default;
	};

	struct WorkspaceState
	{
		std::vector<TabState> Tabs;
		std::optional<std::filesystem::path> Active;

		friend bool operator==(const WorkspaceState&, const WorkspaceState&) = default;
	};
}
