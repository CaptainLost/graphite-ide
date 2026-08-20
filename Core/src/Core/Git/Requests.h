#pragma once

#include "Core/Git/Oid.h"

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

		// Bounds how many commits the walk visits looking for matches, not how many it
		// keeps — MaxCount below is what bounds the result. File and line-range narrowing
		// diff every commit they pass through on the way to a match, so without this a
		// file touched once in a huge history would walk and diff the whole thing. A plain
		// branch log (no File) never diffs, so this rarely matters there.
		std::size_t MaxWalk = 20000;

		std::size_t MaxCount = 500;
	};

	enum class DiffTarget
	{
		// What is edited but not staged.
		WorkTreeToIndex,

		// What is staged and would go into the next commit.
		IndexToHead,

		// Everything that differs from the last commit, staged or not. This is what the
		// editor's change markers are drawn from.
		WorkTreeToHead,

		// One commit against its first parent. A root commit diffs against nothing, and
		// every file comes out as added.
		CommitToParent,

		// Any two revisions, in either direction.
		RevisionToRevision
	};

	struct DiffRequest
	{
		DiffTarget Target = DiffTarget::WorkTreeToHead;

		// CommitToParent reads From as the commit. RevisionToRevision reads From as the
		// older side and To as the newer. The working tree targets read neither.
		std::optional<Oid> From;
		std::optional<Oid> To;

		// Relative to the working directory. Without it, the whole tree.
		std::optional<std::filesystem::path> File;
	};
}
