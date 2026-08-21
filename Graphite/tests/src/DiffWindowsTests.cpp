#include <doctest/doctest.h>

#include "Git/DiffWindows.h"

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
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt"))
		  != Graphite::DiffWindowIdentity(Diff(1, "b.txt")));
}

TEST_CASE("One file in two commits has different identities")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt"))
		  != Graphite::DiffWindowIdentity(Diff(2, "a.txt")));
}

TEST_CASE("An identity is the same every time it is asked for")
{
	// ImGui keys a window's dock position and its imgui.ini entry by this string. An
	// identity that varied between frames would give the window a new home each frame.
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt"))
		  == Graphite::DiffWindowIdentity(Diff(1, "a.txt")));
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
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a/b.txt"))
		  != Graphite::DiffWindowIdentity(Diff(1, "a\\b.txt")));
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
