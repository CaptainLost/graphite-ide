#include <doctest/doctest.h>

#include "GitFixtures.h"
#include "Git/GitView.h"

#include <IconsMaterialDesignIcons.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

	const std::optional<std::size_t> index = Graphite::IndexOfFile(diff, "b.txt");

	REQUIRE(index.has_value());
	CHECK(*index == 1);
}

TEST_CASE("A file that is no longer in the diff is not found, because a refresh can reorder the list and an index would show another file")
{
	const Core::Diff diff = DiffWith({"a.txt"});

	CHECK_FALSE(Graphite::IndexOfFile(diff, "gone.txt").has_value());
}

TEST_CASE("A deleted file is found by its old path, since it has no new path to match")
{
	Core::Diff diff;

	Core::FileDiff file;
	file.OldPath = "gone.txt";
	file.Change = Core::FileChange::Deleted;
	diff.Files.push_back(std::move(file));

	const std::optional<std::size_t> index = Graphite::IndexOfFile(diff, "gone.txt");

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

TEST_CASE("A distance is shown as arrows, up for commits to push and down for commits to pull")
{
	Core::Branch branch = BranchNamed("main");
	branch.DistanceKnown = true;
	branch.Ahead = 2;
	branch.Behind = 3;

	CHECK(Graphite::RefLabel(branch) == std::string{"main  "} + ICON_MDI_ARROW_UP + "2 " + ICON_MDI_ARROW_DOWN + "3");
}

TEST_CASE("Only the side with commits gets an arrow")
{
	Core::Branch branch = BranchNamed("main");
	branch.DistanceKnown = true;
	branch.Ahead = 2;

	CHECK(Graphite::RefLabel(branch) == std::string{"main  "} + ICON_MDI_ARROW_UP + "2");

	branch.Ahead = 0;
	branch.Behind = 3;

	CHECK(Graphite::RefLabel(branch) == std::string{"main  "} + ICON_MDI_ARROW_DOWN + "3");
}

TEST_CASE("A measured distance of zero shows no numbers, because in sync is the ordinary state")
{
	Core::Branch branch = BranchNamed("main");
	branch.Upstream = "origin/main";
	branch.DistanceKnown = true;

	CHECK(Graphite::RefLabel(branch) == "main");
}

TEST_CASE("An unmeasured distance shows no numbers even when the stale counts are not zero")
{
	Core::Branch branch = BranchNamed("main");
	branch.Upstream = "origin/main";
	branch.DistanceKnown = false;
	branch.Ahead = 7;
	branch.Behind = 9;

	const std::string label = Graphite::RefLabel(branch);

	CHECK(label.find("7") == std::string::npos);
	CHECK(label.find("9") == std::string::npos);
}

namespace {

	Core::LogPage PageOf(std::vector<std::uint8_t> commits)
	{
		Core::LogPage page;

		for (const std::uint8_t firstByte : commits)
		{
			Core::CommitSummary commit;
			commit.Id = OidWithFirstByte(firstByte);
			page.Commits.push_back(std::move(commit));
		}

		return page;
	}

	Graphite::FileSelection SelectionIn(std::uint8_t commitFirstByte, const char* path)
	{
		return Graphite::FileSelection{OidWithFirstByte(commitFirstByte), path};
	}
}

TEST_CASE("A selection whose commit is still listed survives a new page, so a refresh does not close the diff being read")
{
	const Core::LogPage page = PageOf({1, 2, 3});

	const std::optional<Graphite::FileSelection> surviving =
		Graphite::SelectionSurviving(SelectionIn(2, "a.cpp"), page);

	REQUIRE(surviving.has_value());
	CHECK(surviving->Path == std::filesystem::path{"a.cpp"});
}

TEST_CASE("A selection whose commit the new page does not list does not survive, as when a narrowed filter drops it")
{
	const Core::LogPage page = PageOf({1, 3});

	CHECK_FALSE(Graphite::SelectionSurviving(SelectionIn(2, "a.cpp"), page).has_value());
}

TEST_CASE("Nothing selected stays nothing selected")
{
	CHECK_FALSE(Graphite::SelectionSurviving(std::nullopt, PageOf({1, 2})).has_value());
}

TEST_CASE("An empty page keeps no selection at all")
{
	CHECK_FALSE(Graphite::SelectionSurviving(SelectionIn(1, "a.cpp"), PageOf({})).has_value());
}

namespace {

	Core::Oid CommitId(std::uint8_t firstByte)
	{
		return OidWithFirstByte(firstByte);
	}
}

TEST_CASE("A selected commit that is still listed survives a new page, following the same rule as the diff pane")
{
	const Core::LogPage page = PageOf({1, 2, 3});

	const std::optional<Core::Oid> surviving = Graphite::CommitSelectionSurviving(CommitId(2), page);

	REQUIRE(surviving.has_value());
	CHECK(*surviving == CommitId(2));
}

TEST_CASE("A selected commit the new page does not list does not survive")
{
	const Core::LogPage page = PageOf({1, 3});

	CHECK_FALSE(Graphite::CommitSelectionSurviving(CommitId(2), page).has_value());
}

TEST_CASE("No commit selected stays no commit selected")
{
	CHECK_FALSE(Graphite::CommitSelectionSurviving(std::nullopt, PageOf({1, 2})).has_value());
}
