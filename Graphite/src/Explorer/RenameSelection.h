#pragma once

#include <cstddef>
#include <string_view>

namespace Graphite {

	struct RenameSpan
	{
		std::size_t Start = 0;
		std::size_t End = 0;

		bool operator==(const RenameSpan&) const = default;
	};

	RenameSpan RenameSelection(std::string_view name, bool isFolder);
}
