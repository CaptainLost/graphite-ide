#pragma once

#include <cstddef>
#include <string_view>

namespace Graphite {

	struct ChangedSpan
	{
		std::size_t From = 0;
		std::size_t To = 0;

		friend bool operator==(const ChangedSpan&, const ChangedSpan&) = default;
	};

	// WARNING: Not a character diff. Two separate edits in one line cover everything between them.
	ChangedSpan RemovedSpan(std::string_view oldLine, std::string_view newLine);
}
