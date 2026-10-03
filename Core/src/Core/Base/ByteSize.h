#pragma once

#include <cstdint>
#include <string>

namespace Core {

	// Sizes as a person reads them, in two shapes chosen by what the number is for.

	// A size read in passing - a message, an attachment: bytes below a kilobyte, then one
	// decimal of the largest unit that fits. "812 B", "3.2 KB", "1.4 MB".
	std::string FormatByteSize(std::uint64_t bytes);

	// A limit or a budget, compared at a glance: the whole number of units, rounded down.
	// "3 KB", "34 MB".
	std::string FormatWholeKilobytes(std::uint64_t bytes);
	std::string FormatWholeMegabytes(std::uint64_t bytes);
}
