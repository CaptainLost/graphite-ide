#pragma once

#include <filesystem>

namespace Graphite {

	// Copies a settings file that does not parse to "<name>.bak", replacing an older copy, and
	// says so in the log. Called before a save goes over such a file: it reads as empty, so the
	// save would otherwise drop everything a typo in a hand edit left unreadable. False, logging
	// nothing, when the copy could not be made; the file must not be written over then, and the
	// caller decides how often to say so.
	bool BackUpMalformedSettings(const std::filesystem::path& file);
}
