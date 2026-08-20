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
