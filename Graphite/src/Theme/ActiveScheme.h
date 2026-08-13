#pragma once

#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"

#include <filesystem>
#include <string_view>

namespace Graphite {

	// Resolves a scheme by name against a directory of scheme files. A pure function of its
	// arguments, rather than reaching for AppSettings itself, so it can be exercised against a
	// temporary directory without touching the file beside the test executable.
	//
	// An empty name, or one ParseBuiltInName recognises, returns that built-in with no file
	// read. Otherwise the name is loaded from directory / (name + ".json") — LoadScheme never
	// fails, and already falls back to the dark base (with a warning) when the file names a
	// base nobody knows.
	ResolvedScheme ResolveNamed(std::string_view name, const std::filesystem::path& directory);

	// The scheme the editor currently draws from. Holds the resolved value rather than a name,
	// so the palettes are rebuilt from something already in memory instead of re-reading the
	// scheme file every frame.
	class ActiveScheme
	{
	public:
		void Load();

		const ResolvedScheme& Get() const { return m_Resolved; }

	private:
		ResolvedScheme m_Resolved = DarkScheme();
	};
}
