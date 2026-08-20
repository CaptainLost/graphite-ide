#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
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

		// The span a language server named, in the units it names them in — UTF-16 code units —
		// held while the line above has not been read yet. Turning it into the byte range above
		// needs the line's own text, so a results list that reads its lines only when a row is
		// drawn has to carry this until then. A hit from a file-tree search arrives with its line
		// already in hand and leaves it unset.
		//
		// Both ends together, in one optional, because they are meaningless apart: the end is
		// what makes the highlight cover the name as written at the site rather than a guess from
		// the symbol's length, and a start carried without it would be exactly that guess again.
		struct UnreadSpan
		{
			std::size_t Column = 0;
			std::size_t EndColumn = 0;
		};

		std::optional<UnreadSpan> Unread;
	};
}
