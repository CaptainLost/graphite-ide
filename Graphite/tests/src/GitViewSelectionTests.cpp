#include <doctest/doctest.h>

#include "Git/GitView.h"

namespace {

	Core::Diff DiffWith(std::vector<const char*> paths)
	{
		Core::Diff diff;

		for (const char* path : paths)
		{
			Core::FileDiff file;
			file.NewPath = path;
			file.Change = Core::FileChange::Modified;
			diff.Files.push_back(std::move(file));
		}

		return diff;
	}
}

TEST_CASE("A file is found by its new path")
{
	const Core::Diff diff = DiffWith({"a.txt", "b.txt"});

	const auto index = Graphite::IndexOfFile(diff, "b.txt");

	REQUIRE(index.has_value());
	CHECK(*index == 1);
}

TEST_CASE("A file that is no longer in the diff is not found")
{
	const Core::Diff diff = DiffWith({"a.txt"});

	// The selection survives a refresh by path rather than by index, because a refresh can
	// reorder the list — matching by index would silently show a different file's diff.
	CHECK_FALSE(Graphite::IndexOfFile(diff, "gone.txt").has_value());
}

TEST_CASE("A deleted file is found by its old path")
{
	Core::Diff diff;

	Core::FileDiff file;
	file.OldPath = "gone.txt";
	file.Change = Core::FileChange::Deleted;
	diff.Files.push_back(std::move(file));

	// A deleted file has no new path at all, so matching only on NewPath would make it
	// unselectable — and a deletion is exactly the change a reader most wants to inspect.
	const auto index = Graphite::IndexOfFile(diff, "gone.txt");

	REQUIRE(index.has_value());
	CHECK(*index == 0);
}

TEST_CASE("Nothing is found in an empty diff")
{
	CHECK_FALSE(Graphite::IndexOfFile(Core::Diff{}, "a.txt").has_value());
}

namespace {

	Core::Branch BranchNamed(const char* name)
	{
		Core::Branch branch;
		branch.Name = name;
		branch.FullName = std::string("refs/heads/") + name;
		branch.Kind = Core::RefKind::Local;
		return branch;
	}
}

TEST_CASE("A branch shows its own name")
{
	CHECK(Graphite::RefLabel(BranchNamed("main")) == "main");
}

TEST_CASE("The checked out branch is marked")
{
	Core::Branch branch = BranchNamed("main");
	branch.IsHead = true;

	CHECK(Graphite::RefLabel(branch).starts_with("*"));
	CHECK(Graphite::RefLabel(branch).find("main") != std::string::npos);
}

TEST_CASE("A measured distance is shown")
{
	Core::Branch branch = BranchNamed("main");
	branch.Upstream = "origin/main";
	branch.DistanceKnown = true;
	branch.Ahead = 2;
	branch.Behind = 3;

	const std::string label = Graphite::RefLabel(branch);

	CHECK(label.find("2") != std::string::npos);
	CHECK(label.find("3") != std::string::npos);
}

TEST_CASE("A measured distance of zero shows no numbers")
{
	Core::Branch branch = BranchNamed("main");
	branch.Upstream = "origin/main";
	branch.DistanceKnown = true;

	// In sync is the ordinary state; a "0/0" beside every branch is noise.
	CHECK(Graphite::RefLabel(branch) == "main");
}

TEST_CASE("An unmeasured distance shows no numbers even when they are not zero")
{
	Core::Branch branch = BranchNamed("main");
	branch.Upstream = "origin/main";
	branch.DistanceKnown = false;
	branch.Ahead = 7;
	branch.Behind = 9;

	// This is the case Core::Branch's own comment warns about: Ahead and Behind hold
	// whatever they were left at, and showing them would state a distance nobody measured.
	const std::string label = Graphite::RefLabel(branch);

	CHECK(label.find("7") == std::string::npos);
	CHECK(label.find("9") == std::string::npos);
}
