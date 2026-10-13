#pragma once

#include <filesystem>
#include <string_view>

namespace Graphite {

	// Copies a file this build cannot use as it is to "<name>.bak", replacing an older copy, and
	// says so in the log, `why` reading after the file's name: "does not parse". Called before a
	// save goes over such a file: nothing of it was read, so the save would otherwise drop
	// everything it held. False, logging nothing, when the copy could not be made; the file must
	// not be written over then, and the caller decides how often to say so.
	bool BackUpBeforeWritingOver(const std::filesystem::path& file, std::string_view why);
}
