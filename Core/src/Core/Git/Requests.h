#pragma once

#include "Core/Git/CommitFilter.h"
#include "Core/Git/Oid.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	// Inclusive on both ends, as an editor numbers its lines, so a single line has First equal to
	// Last. It is not the half-open range the standard library uses.
	struct OneBasedLineRange
	{
		std::size_t First = 1;
		std::size_t Last = 1;
	};

	// The newest commits a history shows before the reader asks for more, and how many commits a
	// walk may visit before it stops looking.
	inline constexpr std::size_t kLogPageSize = 500;
	inline constexpr std::size_t kLogWalkLimit = 20000;

	// A branch log, a file's history and a line range's history differ only in which fields are
	// filled in, so they share this request instead of being three functions.
	struct LogRequest
	{
		// Without a value the walk starts at HEAD. A value must be a full reference name such as
		// "refs/heads/main", not a short one.
		std::optional<std::string> Ref;

		// Leaves out every commit reachable from this one, as `git log <exclude>..<ref>` does: given
		// a branch's fork point, the branch's own commits. Ignored for a stash.
		std::optional<Oid> Exclude;

		// Shows this stash alone instead of walking. Git stores a stash as a merge whose other
		// parents hold the index and the untracked files, so the row names only the first parent;
		// Filter still applies, while File, Lines, the walk bounds and the upstream are ignored.
		std::optional<Oid> Stash;

		// Relative to the working directory; without a value, the whole tree.
		std::optional<std::filesystem::path> File;

		// Ignored without File.
		std::optional<OneBasedLineRange> Lines;

		// Narrows the page, not the walk: every commit up to MaxWalk is still visited and measured,
		// so a filter that matches nothing costs a full walk. An empty filter accepts everything.
		CommitFilter Filter;

		bool FollowRenames = true;

		// Bounds the commits visited, not the commits kept, which MaxCount bounds. A file or line
		// history diffs every commit it passes, so without it a file touched once in a long
		// history would diff all of that history.
		std::size_t MaxWalk = kLogWalkLimit;

		std::size_t MaxCount = kLogPageSize;
	};

	enum class DiffTarget
	{
		WorkTreeToIndex,
		IndexToHead,
		WorkTreeToHead,

		// Against the first parent only. A root commit diffs against nothing, so every file comes
		// out as added.
		CommitToParent,

		// The stash's tracked changes against the commit it was made on, together with the
		// untracked files the stash keeps in its third parent, each of them added.
		StashToParent,

		RevisionToRevision
	};

	// Whether a diff counts lines that differ only in white space, which `git diff -w` does not.
	enum class WhitespaceInDiff
	{
		Shown,
		Ignored
	};

	struct DiffRequest
	{
		DiffTarget Target = DiffTarget::WorkTreeToHead;

		// CommitToParent and StashToParent read From as the commit or the stash; RevisionToRevision
		// reads From as the older side and To as the newer. The working tree targets read neither.
		std::optional<Oid> From;
		std::optional<Oid> To;

		// Relative to the working directory; without a value, the whole tree.
		std::optional<std::filesystem::path> File;

		WhitespaceInDiff Whitespace = WhitespaceInDiff::Shown;

		friend bool operator==(const DiffRequest&, const DiffRequest&) = default;
	};

	struct CommitRequest
	{
		std::string Message;

		// Replaces HEAD, keeping its parents, and fails on an unborn HEAD. With Amend an empty
		// Message keeps the replaced commit's message; without it an empty Message is refused.
		bool Amend = false;
	};

	enum class UntrackedFiles
	{
		Excluded,
		Included
	};

	struct GraphReadRequest
	{
		std::vector<Oid> Tips;  // walk from these
		std::vector<Oid> Known; // commits already in the graph: they and their ancestors are not read again; one git lacks is passed over

		friend bool operator==(const GraphReadRequest&, const GraphReadRequest&) = default;
	};
}
