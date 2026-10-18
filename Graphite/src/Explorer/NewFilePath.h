#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// What a typed name asks for: the folders to create on the way, then the file in the last of them.
	struct NewFilePath
	{
		std::vector<std::string> Folders;
		std::string File;

		bool operator==(const NewFilePath&) const = default;
	};

	// WARNING: Empty when any piece is not a valid file name, so "a//b", "a/" and "../x" all name nothing.
	std::optional<NewFilePath> ParseNewFilePath(std::string_view typed);
}
