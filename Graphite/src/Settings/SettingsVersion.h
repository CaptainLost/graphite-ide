#pragma once

namespace Core {
	class JsonFile;
}

namespace Graphite {

	// The shape of graphite.json this build writes. Raised when a later build changes what an
	// existing key means, at which point reading an older file gains a migration step.
	inline constexpr int kSettingsVersion = 1;

	// Marks the file as written by this build, and says whether it may be written at all.
	// False, leaving the file untouched, when a newer build wrote it: that build may have given
	// a key a meaning this one does not know, and writing it back in the old meaning would
	// corrupt the settings the newer build relies on. A missing or unreadable version counts
	// as older - it is what every file written before versioning looks like.
	bool StampSettingsVersion(Core::JsonFile& file);
}
