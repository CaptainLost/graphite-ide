#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// The branch a stash was made on, read from the prefix git writes into its message:
	// "On <branch>: <message>" or "WIP on <branch>: <commit summary>". The branch runs to the
	// first colon, which a branch name cannot contain. Nothing for a message without that
	// prefix - one written by a tool that sets its own.
	std::optional<std::string> StashBranch(std::string_view message);

	// What one row of the stash list says.
	struct StashLabel
	{
		std::string Text;

		// False when the stash has no message of the reader's own and Text is only when and
		// where it was made - drawn dimmed, so it does not read as a stash named "main".
		bool Named = false;
	};

	// The reader's own message, from "On <branch>: <message>", when they gave one. Otherwise
	// `when` followed by the branch: "WIP on <branch>: <commit summary>" is what git writes
	// for a stash made without a message, and the summary there names the commit the stash
	// was made on rather than anything in it. When comes first because it is short and tells
	// two such stashes apart, so it is what a narrow column should keep. A message without
	// git's prefix is shown as it is. Only its first line, since a row is one line.
	StashLabel StashRowLabel(std::string_view message, std::string_view when);

	// How the drop question names a stash: `"<message>" (stash@{N})`, or just `stash@{N}` for
	// a stash with no message of its own. The message is cut to `maxChars` characters with an
	// ellipsis, on a character boundary, so a long one does not turn the title into a line.
	std::string DropStashSubject(std::size_t index, std::string_view message, std::size_t maxChars = 48);
}
