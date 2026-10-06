#pragma once

#include <cstdint>
#include <string>

namespace Core {

	std::string FormatByteSize(std::uint64_t bytes);

	std::string FormatWholeKilobytes(std::uint64_t bytes);
	std::string FormatWholeMegabytes(std::uint64_t bytes);
}
