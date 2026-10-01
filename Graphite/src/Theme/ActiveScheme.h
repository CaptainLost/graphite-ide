#pragma once

#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Graphite {

	class AppSettings;

	// Resolves a scheme by name against a directory of scheme files. A pure function of its
	// arguments, rather than reaching for AppSettings itself, so it can be exercised against a
	// temporary directory without touching any settings file.
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
		// Resolves the scheme `settings` names against the user's themes directory.
		void Load(const AppSettings& settings);

		const ResolvedScheme& Get() const { return m_Resolved; }

		// The settings section previews by writing here: GraphiteLayer already hands Get() to the
		// editor every frame, so the next frame draws whatever was last set. There is no preview
		// mode to leave switched on — this holds one resolved scheme and always has.
		void Set(const ResolvedScheme& resolved)
		{
			m_Resolved = resolved;
			++m_Version;
		}

		// Bumped by every write, so a reader can tell "the colours changed" from "the colours
		// happen to compare equal". A counter rather than a comparison: ResolvedScheme is two
		// arrays and a third, and comparing them per frame to answer a question a counter
		// answers for free is work nobody asked for.
		std::uint32_t GetVersion() const { return m_Version; }

	private:
		ResolvedScheme m_Resolved = DarkScheme();

		std::uint32_t m_Version = 1;
	};
}
