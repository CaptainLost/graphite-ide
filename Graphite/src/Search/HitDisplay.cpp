#include "Search/HitDisplay.h"

#include "Core/FileSystem/FileSystem.h"

#include <algorithm>

namespace Graphite {

	namespace {

		bool IsIndentCharacter(char character)
		{
			return character == ' ' || character == '\t';
		}
	}

	HighlightedLabel SplitForDisplay(const SearchHit& hit)
	{
		const std::string_view text = hit.Text;

		const std::size_t clampedFrom = std::min(hit.From, text.size());
		const std::size_t clampedTo = std::min(std::max(hit.To, clampedFrom), text.size());

		std::size_t indentLength = 0;

		while (indentLength < clampedFrom && IsIndentCharacter(text[indentLength]))
		{
			++indentLength;
		}

		const std::string_view before = text.substr(indentLength, clampedFrom - indentLength);
		const std::string_view match = text.substr(clampedFrom, clampedTo - clampedFrom);
		const std::string_view after = text.substr(clampedTo);

		return HighlightedLabel{before, match, after};
	}

	DisplayPath SplitPathForDisplay(const std::filesystem::path& file,
									const std::filesystem::path& root)
	{
		DisplayPath displayPath;
		displayPath.Name = Core::ToUtf8(file.filename());

		std::filesystem::path directory = file.parent_path();

		if (!root.empty())
		{
			// WARNING: lexically_relative, not relative. This runs per drawn group, and the
			// filesystem-touching overload would stat on every frame.
			const std::filesystem::path relativeDirectory = directory.lexically_relative(root);

			const bool sharesRoot = !relativeDirectory.empty();
			const bool isInsideRoot = sharesRoot && *relativeDirectory.begin() != "..";
			const bool isRootItself = relativeDirectory == ".";

			if (isInsideRoot)
			{
				directory = isRootItself ? std::filesystem::path{} : relativeDirectory;
			}
		}

		displayPath.Directory = Core::ToUtf8(directory);

		if (!displayPath.Directory.empty())
		{
			const char separator = static_cast<char>(std::filesystem::path::preferred_separator);

			displayPath.Directory += separator;
		}

		return displayPath;
	}
}
