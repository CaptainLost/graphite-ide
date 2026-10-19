#include <doctest/doctest.h>

#include "Git/Diff/DiffWindows.h"
#include "UI/FileIcons.h"

#include "GitFixtures.h"

#include "Core/Git/Status.h"

#include <string>

namespace {

	Graphite::OpenDiff Diff(std::uint8_t commit, const char* path)
	{
		return Graphite::OpenDiff{.Commit = OidWithFirstByte(commit),
								  .Target = Core::DiffTarget::CommitToParent,
								  .Path = path};
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
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a.txt")) == Graphite::DiffWindowIdentity(Diff(1, "a.txt")));
}

TEST_CASE("An identity is the commit's full id immediately followed by the path")
{
	const std::string identity = Graphite::DiffWindowIdentity(Diff(1, "a.txt"));

	CHECK(identity == OidWithFirstByte(1).ToHex() + "a.txt");
}

TEST_CASE("Paths that differ only in their separators are told apart")
{
	CHECK(Graphite::DiffWindowIdentity(Diff(1, "a/b.txt")) != Graphite::DiffWindowIdentity(Diff(1, "a\\b.txt")));
}

TEST_CASE("The title carries the file's name and the commit's short id")
{
	const std::string title = Graphite::DiffWindowTitle(Diff(0xAB, "src/Editor/Caret.cpp"));

	CHECK(title.find("Caret.cpp") != std::string::npos);
	CHECK(title.find("ab") != std::string::npos);

	CHECK(title.find("Editor") == std::string::npos);
}

TEST_CASE("A window name shows the title and identifies by the identity")
{
	const Graphite::OpenDiff diff = Diff(1, "a.txt");
	const std::string name = Graphite::DiffWindowName(diff);

	const std::size_t marker = name.find("###");

	REQUIRE(marker != std::string::npos);
	CHECK(name.substr(marker + 3) == Graphite::DiffWindowIdentity(diff));
	CHECK(name.find(Graphite::DiffWindowTitle(diff)) < marker);
}

TEST_CASE("A diff's window name starts with its file kind's glyph and keeps its identity after the marker")
{
	const Graphite::OpenDiff diff = Diff(1, "docs/README.md");
	const std::string name = Graphite::DiffWindowName(diff);

	CHECK(name.starts_with(Graphite::GlyphOf(Graphite::FileKind::Markdown)));
	CHECK(name.ends_with("###" + Graphite::DiffWindowIdentity(diff)));
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
	const Graphite::OpenDiff diff = Diff(1, "a.txt");

	CHECK(Graphite::DiffWindowIdentity(diff) == OidWithFirstByte(1).ToHex() + "a.txt");
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
									 Core::StatusFlags workTree, bool isConflicted = false)
	{
		Core::WorkingTreeStatus status;
		status.Entries.push_back(Core::FileStatus{.Path = path,
												  .Index = index,
												  .WorkTree = workTree,
												  .IsConflicted = isConflicted});
		return status;
	}
}

TEST_CASE("A staged tab whose file was unstaged follows it to the unstaged side")
{
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
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::IndexToHead).has_value());
	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A tab whose file has no change anywhere stays where it is")
{
	CHECK(!Graphite::TargetHolding(Core::WorkingTreeStatus{}, "a.txt",
								   Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("A conflicted file's tab stays where it is")
{
	constexpr bool conflicted = true;
	const Core::WorkingTreeStatus status =
		StatusOf("a.txt", Core::StatusFlags::Modified, Core::StatusFlags::Modified, conflicted);

	CHECK(!Graphite::TargetHolding(status, "a.txt", Core::DiffTarget::WorkTreeToIndex)
			   .has_value());
}

TEST_CASE("Only the staged and unstaged pair follow anything")
{
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
	CHECK(Graphite::EmptyDiffMessage(Diff(1, "a.txt")) == "That file is not in this diff.");
}
