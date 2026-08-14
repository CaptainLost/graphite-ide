#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace Graphite {

	struct SearchQuery
	{
		std::string Needle;

		// Semicolon-separated name patterns with * and ?, matched against the file name alone —
		// "*.cpp;*.h". Empty accepts every file.
		std::string Mask;

		bool MatchCase = false;
		bool WholeWord = false;
	};

	// Lives here rather than beside SearchService so SearchSettings can hold one without
	// including the job queue, which it has no business knowing about.
	struct SearchOptions
	{
		bool RespectGitIgnore = true;
		bool ProjectsOnly = false;
	};

	struct SearchHit
	{
		std::filesystem::path File;

		// Zero-based, so it indexes a document's lines directly; the panel adds one to show it.
		std::size_t Line = 0;

		// The whole line the hit sits on, because the results list shows it and re-reading the
		// file to get it back would be reading it twice.
		std::string Text;

		// Byte range within Text. The editor selects it on a jump.
		std::size_t From = 0;
		std::size_t To = 0;
	};
}
