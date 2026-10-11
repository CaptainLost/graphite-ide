#include <doctest/doctest.h>

#include "Language/Server/SolutionForRoot.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <optional>
#include <ostream>

TEST_CASE("The selected solution is used when it lies inside the root")
{
	TempDirectory temp;
	temp.MakeFile("repo/A.sln", "");
	temp.MakeFile("repo/src/Selected.slnx", "");

	const std::optional<std::filesystem::path> solution =
		Graphite::SolutionForRoot(temp.GetPath() / "repo", temp.GetPath() / "repo" / "src" / "Selected.slnx");

	CHECK(solution == temp.GetPath() / "repo" / "src" / "Selected.slnx");
}

TEST_CASE("A root whose selected solution lies elsewhere uses its own first solution by name")
{
	TempDirectory temp;
	temp.MakeFile("repo/Zeta.slnx", "");
	temp.MakeFile("repo/alpha.sln", "");
	temp.MakeFile("other/Other.sln", "");

	const std::optional<std::filesystem::path> solution =
		Graphite::SolutionForRoot(temp.GetPath() / "repo", temp.GetPath() / "other" / "Other.sln");

	CHECK(solution == temp.GetPath() / "repo" / "alpha.sln");
}

TEST_CASE("A root with a solution in both formats uses the newer one")
{
	TempDirectory temp;
	temp.MakeFile("repo/Demo.sln", "");
	temp.MakeFile("repo/Demo.slnx", "");

	CHECK(Graphite::SolutionForRoot(temp.GetPath() / "repo", {}) == temp.GetPath() / "repo" / "Demo.slnx");
}

TEST_CASE("A root holding no solution file has no solution")
{
	TempDirectory temp;
	std::filesystem::create_directories(temp.GetPath() / "repo" / "fake.sln");
	temp.MakeFile("repo/src/Nested.sln", "");

	CHECK_FALSE(Graphite::SolutionForRoot(temp.GetPath() / "repo", {}).has_value());
}
