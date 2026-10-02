#pragma once

#include "Core/Git/CommitFilter.h"
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

		// This stash alone, in place of a walk: a stash belongs to no branch, and the history
		// it was made on is that branch's to show. Git keeps a stash as a merge of the commit
		// it was made on with one commit holding the index and, for a stash that took
		// untracked files, another holding those. Neither is anything the reader made, so
		// the row names its first parent alone. Filter applies; File, Lines and the walk
		// bounds have nothing to narrow, and nothing is measured against an upstream.
		std::optional<Oid> Stash;

		// Relative to the working directory. Without it, the whole tree.
		std::optional<std::filesystem::path> File;

		// Only meaningful together with File.
		std::optional<LineRange> Lines;

		// Narrows the page to the commits this matches, without narrowing the walk: every
		// commit the walk reaches is still visited and measured, so MaxWalk below is what
		// bounds the search, and a filter that matches nothing still costs a full walk up
		// to that ceiling. An empty filter accepts everything, which is what makes this
		// field free to leave alone.
		CommitFilter Filter;

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

		// A stash against the commit it was made on: what CommitToParent gives for its own
		// commit, which is the tracked files, together with the untracked files a stash
		// made to include them keeps in its third parent, each of them added. Reads From
		// as the stash.
		StashToParent,

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

		friend bool operator==(const DiffRequest&, const DiffRequest&) = default;
	};

	// What a commit needs and nothing more. No author override, no sign-off, no signing:
	// none is needed to make the panel work, and each is easier to add against a shipped,
	// tested write path than to design blind now.
	struct CommitRequest
	{
		std::string Message;

		// Replaces HEAD rather than adding to it, keeping HEAD's parents. Fails on an
		// unborn HEAD, which has nothing to replace.
		//
		// An empty Message keeps the message of the commit being replaced, rather than
		// blanking it - amending to add a forgotten file is the case with nothing new to
		// say. Without Amend an empty Message is just an empty message, which git rejects.
		bool Amend = false;
	};
}
