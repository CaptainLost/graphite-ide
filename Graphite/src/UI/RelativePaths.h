#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	// The message with every absolute path inside `root` written relative to it, for a toast
	// about that folder or its repository: "D:/Repos/IDE/.claude/x" reads ".claude/x", which
	// leaves the line to what differs. Only whole paths - a word that starts with the root, or
	// a quoted one - and only ones strictly inside it; the root itself, a sibling such as
	// "D:/Repos/IDE2/x" and anything elsewhere stay as they are. Either separator matches
	// either, in the message and in the root alike, and a drive letter matches in either case.
	// An empty root changes nothing.
	std::string RelativiseUnder(std::string_view message, const std::filesystem::path& root);
}
