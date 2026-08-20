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
		return Graphite::OpenDiff{IdOf(commit), path};
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

TEST_CASE("An identity begins with the commit's full id")
{
	const std::string identity = Graphite::DiffWindowIdentity(Diff(1, "a.txt"));

	// A full object name is always forty characters, so a commit and a path concatenated
	// in that order cannot be split two ways — which is what makes the identity unique
	// without a separator that a path might itself contain.
	CHECK(identity.starts_with(IdOf(1).ToHex()));
	CHECK(identity.size() > IdOf(1).ToHex().size());
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
