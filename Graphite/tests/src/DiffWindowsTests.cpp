#include <doctest/doctest.h>

#include "Git/Diff/DiffWindows.h"

#include "Core/Git/Status.h"

#include <string>

namespace {

	Core::Oid IdOf(std::uint8_t value)
	{
		Core::Oid id;
		id.Bytes[0] = value;
		return id;
	}

	Graphite::OpenDiff Diff(std::uint8_t commit, const char* path)
	{
		return Graphite::OpenDiff{
			.Commit = IdOf(commit), .Target = Core::DiffTarget::CommitToParent, .Path = path};
	}
}

TEST_CASE("Two files of one commit have different identities")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) != Graphite::DiffWindowIdentity(Diff(1, "b.txt")));
}

TEST_CASE("One file in two commits has different identities")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) != Graphite::DiffWindowIdentity(Diff(2, "a.txt")));
}

TEST_CASE("An identity is the same every time it is asked for")
{
	// ImGui keys a window's dock position and its imgui.ini entry by this string. An
	// identity that varied between frames would give the window a new home each frame.
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) == Graphite::DiffWindowIdentity(Diff(1, "a.txt")));
}

TEST_CASE("An identity is the commit's full id immediately followed by the path")
{
	const std::string identity = Graphite::DiffWindowIdentity(Diff(1, "a.txt"));

	// Pinned as an exact equality, not merely a prefix check: a scheme that joined the two
	// with a separator (say "|") would also start with the hex id and be longer than it,
	// so those two assertions alone would not catch the very design the no-separator
	// scheme exists to avoid. A full object name is always forty characters, so this
	// concatenation cannot be split two ways.
	CHECK(identity == IdOf(1).ToHex() + "a.txt");
}

TEST_CASE("Paths that differ only in their separators are told apart")
{
	// A hand-rolled identity that normalised separators would give these one window
	// between them; one that does not must keep them apart. Either is defensible, but it
	// has to be decided rather than left to chance, and this is where it is decided:
	// they are different paths and they get different windows.
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a/b.txt")) != Graphite::DiffWindowIdentity(Diff(1, "a\\b.txt")));
}

TEST_CASE("The title carries the file's name and the commit's short id")
{
	const std::string title = Graphite::DiffWindowTitle(Diff(0xAB, "src/Editor/Caret.cpp"));

	CHECK(title.find("Caret.cpp") != std::string::npos);
	CHECK(title.find("ab") != std::string::npos);

	// The directory would push the part that identifies the tab off the end of a narrow
	// tab strip, and the same file's diff in two commits is told apart by the id, not by
	// the path.
	CHECK(title.find("Editor") == std::string::npos);
}

TEST_CASE("A window name shows the title and identifies by the identity")
{
	const Graphite::OpenDiff diff = Diff(1, "a.txt");
	const std::string name = Graphite::DiffWindowName(diff);

	// ImGui shows what precedes ### and identifies by what follows.
	const std::size_t marker = name.find("###");

	REQUIRE(marker != std::string::npos);
	CHECK(name.substr(marker + 3) == Graphite::DiffWindowIdentity(diff));
	CHECK(name.find(Graphite::DiffWindowTitle(diff)) < marker);
}

TEST_CASE("A working-tree diff and a commit's diff of the same file are different tabs")
{
	const Graphite::OpenDiff fromCommit{
		.Commit = Core::Oid{}, .Target = Core::DiffTarget::CommitToParent, .Path = "a.txt"};

	const Graphite::OpenDiff fromWorkTree{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	CHECK(Graphite::DiffWindowIdentity(fromCommit) != Graphite::DiffWindowIdentity(fromWorkTree));
}

TEST_CASE("The staged and unstaged views of one file are different tabs")
{
	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	const Graphite::OpenDiff staged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::IndexToHead, .Path = "a.txt"};

	CHECK(Graphite::DiffWindowIdentity(unstaged) != Graphite::DiffWindowIdentity(staged));
}

TEST_CASE("A working-tree tab is titled by its file and what it compares")
{
	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "src/a.txt"};

	const std::string title = Graphite::DiffWindowTitle(unstaged);

	CHECK(title.find("a.txt") != std::string::npos);
	CHECK(title.find("src") == std::string::npos);
}

TEST_CASE("A commit tab's identity is unchanged from before OpenDiff could name the working tree")
{
	// Pins the byte-for-byte compatibility this task promises: an already-docked commit tab
	// must keep the same imgui.ini entry, so its identity must still be exactly the commit's
	// full hex id immediately followed by the path.
	const Graphite::OpenDiff diff = Diff(1, "a.txt");

	CHECK(Graphite::DiffWindowIdentity(diff) == IdOf(1).ToHex() + "a.txt");
}

TEST_CASE("Opening the same working-tree diff twice adds one tab")
{
	Graphite::DiffWindows windows;

	const Graphite::OpenDiff unstaged{
		.Commit = std::nullopt, .Target = Core::DiffTarget::WorkTreeToIndex, .Path = "a.txt"};

	windows.Open(unstaged);
	windows.Open(unstaged);

	CHECK(windows.GetCount() == 1);
}

namespace {

	Graphite::OpenDiff WorkingDiff(Core::DiffTarget target, const char* path)
	{
		return Graphite::OpenDiff{.Commit = std::nullopt, .Target = target, .Path = path};
	}

	Core::WorkingTreeStatus StatusOf(const char* path, Core::StatusFlags index,
									 Core::StatusFlags workTree, bool conflicted = false)
	{
		Core::WorkingTreeStatus status;
		status.Entries.push_back(Core::FileStatus{.Path = path,
												  .Index = index,
												  .WorkTree = workTree,
												  .IsConflicted = conflicted});
		return status;
	}
}

TEST_CASE("A staged tab whose file was unstaged follows it to the unstaged side")
{
	// The whole point of the tab following: unstaging is not the change vanishing, it is
	// the change moving to the other half of the same file, and that is what the reader
	// wants to go on looking at.
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::None, Core::StatusFlags::Modified);

	CHECK(Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::IndexToHead) == Core::DiffTarget::WorkTreeToIndex);
}

TEST_CASE("An unstaged tab whose file was staged follows it to the staged side")
{
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::None);

	CHECK(Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex) == Core::DiffTarget::IndexToHead);
}

TEST_CASE("A tab whose own side still holds a change stays where it is")
{
	// A file staged one way and edited again since is in both halves at once. Following
	// on that would drag the tab off the side the reader chose, every frame.
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::IndexToHead).has_value());
	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A tab whose file has no change anywhere stays where it is")
{
	// Discarded, or committed: there is nowhere to follow to, so the tab keeps its side
	// and says that side is empty rather than pretending to be the other one.
	CHECK(!Graphite::TargetHolding(Core::WorkingTreeStatus{}, "a.txt",
								   Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A conflicted file's tab stays where it is")
{
	// A conflicted entry is not two ordinary halves - see ChangesRows.h - so its index and
	// work-tree flags are no basis for deciding which half a tab belongs to.
	const Core::WorkingTreeStatus status = StatusOf(
		"a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified, /*conflicted=*/true);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("Only the staged and unstaged pair follow anything")
{
	// WorkTreeToHead spans both halves, and a commit's tab has no working-tree side at
	// all; neither has an other side to move to.
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::None);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToHead).has_value());
	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::CommitToParent).has_value());
}

TEST_CASE("An empty working-tree diff says which side is empty, and for which file")
{
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::IndexToHead, "src/a.txt")) == "a.txt has no staged changes.");
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::WorkTreeToIndex, "src/a.txt")) == "a.txt has no unstaged changes.");
	CHECK(Graphite::EmptyDiffMessage(WorkingDiff(Core::DiffTarget::WorkTreeToHead, "src/a.txt")) == "a.txt has no changes.");
}

TEST_CASE("An empty commit diff still reports a file missing from that commit")
{
	// A commit's diff is fixed: a file absent from it was never part of it, which is a
	// different thing to report than a side of the working tree being momentarily empty.
	CHECK(Graphite::EmptyDiffMessage(Diff(1, "a.txt")) == "That file is not in this diff.");
}
