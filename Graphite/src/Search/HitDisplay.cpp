#include "Search/HitDisplay.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool IsIndent(char c)
		{
			return c == ' ' || c == '\t';
		}
	}

	HitLine SplitForDisplay(const SearchHit& hit)
	{
		const std::string_view text = hit.Text;

		// Clamped rather than trusted: the hit's range was computed against the file as it was
		// read, and nothing stops a caller handing this a range from somewhere else.
		const std::size_t from = std::min(hit.From, text.size());
		const std::size_t to = std::min(std::max(hit.To, from), text.size());

		std::size_t indent = 0;

		// Stops at `from` as well as at the first non-indent character, so a match that lies
		// inside the indentation keeps the whitespace it matched.
		while (indent < from && IsIndent(text[indent]))
		{
			++indent;
		}

		return HitLine{text.substr(indent, from - indent), text.substr(from, to - from),
					   text.substr(to)};
	}

	DisplayPath SplitPathForDisplay(const std::filesystem::path& file,
									const std::filesystem::path& root)
	{
		DisplayPath split;
		split.Name = Core::ToUtf8(file.filename());

		std::filesystem::path directory = file.parent_path();

		if (!root.empty())
		{
			// lexically_relative rather than relative: this runs per drawn group, and the
			// filesystem-touching overload would stat on every frame to answer a question about
			// two strings.
			const std::filesystem::path nearer = directory.lexically_relative(root);

			// Empty means the two share no root at all; a leading ".." means the file is outside
			// the workspace. Either way the absolute path is the shorter and clearer of the two.
			if (!nearer.empty() && *nearer.begin() != "..")
			{
				directory = nearer == "." ? std::filesystem::path{} : nearer;
			}
		}

		split.Directory = Core::ToUtf8(directory);

		if (!split.Directory.empty())
		{
			split.Directory += static_cast<char>(std::filesystem::path::preferred_separator);
		}

		return split;
	}
}
