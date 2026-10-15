#pragma once

#include <cstdint>
#include <string>

namespace Core {

	// Every digit, in groups of three from the right: 1739 reads "1,739".
	std::string FormatCount(std::uint64_t count);
}
