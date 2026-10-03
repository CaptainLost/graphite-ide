#pragma once

#include <cstddef>
#include <string_view>

namespace Graphite {

	// The bytes of a name that start out selected when its inline rename opens.
	struct RenameSpan
	{
		std::size_t Start = 0;
		std::size_t End = 0;

		bool operator==(const RenameSpan&) const = default;
	};

	// A file is renamed far more often by stem than by extension, so only the stem is
	// selected: up to the last dot. A folder, a dotfile and a name without a dot have no
	// extension to spare and are selected whole.
	RenameSpan RenameSelection(std::string_view name, bool isFolder);
}
