#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	// One-based and inclusive on both ends, matching how an editor names its lines.
	struct LineRange
	{
		std::size_t First = 1;
		std::size_t Last = 1;
	};

	// One structure covers three questions: the log of a branch, the history of a file,
	// and the history of a range of lines. They differ only in how much is filled in, so
	// they are not three functions.
	struct LogRequest
	{
		// Empty means HEAD. Otherwise a full name such as "refs/heads/main".
		std::optional<std::string> Ref;

		// Relative to the working directory. Without it, the whole tree.
		std::optional<std::filesystem::path> File;

		// Only meaningful together with File.
		std::optional<LineRange> Lines;

		bool FollowRenames = true;

		std::size_t MaxCount = 500;
	};
}
