#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	std::optional<std::string> StashBranch(std::string_view message);

	struct StashLabel
	{
		std::string Text;
		bool Named = false;
	};

	StashLabel StashRowLabel(std::string_view message, std::string_view when);

	std::string DropStashSubject(std::size_t index, std::string_view message, std::size_t maxChars = 48);

	// "(3)" beside the Stashes heading, and nothing while the list has not been read.
	std::string StashSectionCount(std::optional<std::size_t> stashCount);
}
