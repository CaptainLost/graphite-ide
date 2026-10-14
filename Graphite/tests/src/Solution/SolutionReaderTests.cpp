#include <doctest/doctest.h>

#include "Solution/SolutionReader.h"

#include "SolutionFixtures.h"
#include "TempDirectory.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using Graphite::Folder;
using Graphite::Project;
using Graphite::Solution;

namespace {
	constexpr const char* kSampleSlnx = R"(<Solution Version="1.4">
  <Configurations>
    <BuildType Name="Debug" />
    <Platform Name="x64" />
  </Configurations>
  <Folder Name="/App/">
    <Project Path="IDE\IDE.vcxproj" Id="77F2870B" DefaultStartup="true" />
  </Folder>
  <Folder Name="/Engine/">
    <Project Path="Core\Core.vcxproj" Id="2EB4837C" />
    <Project Path="Core\tests\CoreTests.vcxproj" Id="21E66A18" />
  </Folder>
</Solution>
)";

	const Project* FindProjectNamed(const std::vector<Project>& projects, std::string_view displayName)
	{
		for (const Project& project : projects)
		{
			if (project.DisplayName == displayName)
			{
				return &project;
			}
		}

		return nullptr;
	}
}

TEST_CASE("Reading a solution that does not exist fails with NotFound")
{
	TempDirectory temp;

	const Core::Result<Solution> readResult = Graphite::ReadSolution(temp.GetPath() / "missing.slnx");

	REQUIRE_FALSE(readResult.HasValue());
	CHECK(readResult.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Reading a file with an unknown extension is unsupported")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("thing.cmake", "x");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE_FALSE(readResult.HasValue());
	CHECK(readResult.GetError().Code == Core::ErrorCode::Unsupported);
}

TEST_CASE("Malformed XML reports an error rather than a partial solution")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Broken.slnx", "<Solution><Folder Name=");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE_FALSE(readResult.HasValue());
	CHECK(readResult.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("The solution takes its name from the file")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("MyApp.slnx", kSampleSlnx);

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE(readResult.HasValue());
	CHECK(readResult.GetValue().Name == "MyApp");
	CHECK(readResult.GetValue().Path == solutionFile);
}

TEST_CASE("Solution folders become tree folders named without the slashes that carry their nesting")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("MyApp.slnx", kSampleSlnx);

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());

	const Folder& root = readResult.GetValue().Root;
	REQUIRE(root.Folders.size() == 2);

	CHECK(FindChildFolder(root, "App") != nullptr);
	CHECK(FindChildFolder(root, "Engine") != nullptr);
}

TEST_CASE("Projects land in their folder with an absolute path, though the file writes them relative with backslashes")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("MyApp.slnx", kSampleSlnx);

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());

	const Folder* engine = FindChildFolder(readResult.GetValue().Root, "Engine");
	REQUIRE(engine != nullptr);
	REQUIRE(engine->Projects.size() == 2);

	const Project& core = engine->Projects[0];
	CHECK(core.DisplayName == "Core");
	CHECK(core.Path == temp.GetPath() / "Core" / "Core.vcxproj");
	CHECK(core.Path.is_absolute());
}

TEST_CASE("A child folder declared before its parent is nested in the model by its path, not by element nesting")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Nested.slnx", R"(<Solution>
  <Folder Name="/Group/Inner/">
    <Project Path="a\A.vcxproj" />
  </Folder>
  <Folder Name="/Group/" />
</Solution>
)");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());

	const Folder* group = FindChildFolder(readResult.GetValue().Root, "Group");
	REQUIRE(group != nullptr);
	REQUIRE(group->Folders.size() == 1);
	CHECK(group->Folders[0].Name == "Inner");
	CHECK(group->Folders[0].Projects.size() == 1);
}

TEST_CASE("A project outside any folder sits at the solution root")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Flat.slnx",
															 R"(<Solution><Project Path="a\A.vcxproj" /></Solution>)");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());

	CHECK(readResult.GetValue().Root.Folders.empty());
	REQUIRE(readResult.GetValue().Root.Projects.size() == 1);
	CHECK(readResult.GetValue().Root.Projects[0].DisplayName == "A");
}

TEST_CASE("An empty solution reads as empty rather than failing")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Empty.slnx", "<Solution />");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE(readResult.HasValue());
	CHECK(readResult.GetValue().ProjectCount == 0);
	CHECK(readResult.GetValue().Root.Folders.empty());
	CHECK(readResult.GetValue().Root.Projects.empty());
}

TEST_CASE("The project count covers every folder")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("MyApp.slnx", kSampleSlnx);

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE(readResult.HasValue());
	CHECK(readResult.GetValue().ProjectCount == 3);
}

TEST_CASE("A project whose file is missing is marked rather than dropped, whatever its place in the sorted model")
{
	TempDirectory temp;
	temp.MakeFile("real/Real.vcxproj", "<Project />");
	const std::filesystem::path solutionFile = temp.MakeFile("Mixed.slnx", R"(<Solution>
  <Project Path="real\Real.vcxproj" />
  <Project Path="gone\Gone.vcxproj" />
</Solution>
)");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());
	REQUIRE(readResult.GetValue().Root.Projects.size() == 2);

	const std::vector<Project>& projects = readResult.GetValue().Root.Projects;

	const Project* realProject = FindProjectNamed(projects, "Real");
	const Project* goneProject = FindProjectNamed(projects, "Gone");

	REQUIRE(realProject != nullptr);
	REQUIRE(goneProject != nullptr);

	CHECK(realProject->Exists);
	CHECK_FALSE(goneProject->Exists);
}

TEST_CASE("Project contents are read by the solution reader")
{
	TempDirectory temp;

	temp.MakeFile("App/App.vcxproj", R"(<?xml version="1.0"?>
<Project>
  <ItemGroup>
    <ClCompile Include="Main.cpp" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="..\Core\Core.vcxproj" />
  </ItemGroup>
</Project>
)");

	const std::filesystem::path solutionFile = temp.MakeFile("Real.slnx",
															 R"(<Solution><Project Path="App\App.vcxproj" /></Solution>)");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());
	REQUIRE(readResult.GetValue().Root.Projects.size() == 1);

	const Project& project = readResult.GetValue().Root.Projects[0];

	REQUIRE(project.Exists);
	CHECK(project.ContentRead);
	CHECK_FALSE(project.Content.Root.Files.empty());
	CHECK(project.Content.Root.Folders.empty());
	CHECK_FALSE(project.Content.References.empty());
}

TEST_CASE("An uppercase extension is still a solution because the filesystem ignores case")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Shouty.SLNX", "<Solution />");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	CHECK(readResult.HasValue());
}

TEST_CASE("Valid XML that is not a solution is rejected as the wrong file, not a corrupt one")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Wrong.slnx", "<Project><ItemGroup /></Project>");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);

	REQUIRE_FALSE(readResult.HasValue());
	CHECK(readResult.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Folders and projects come back sorted without regard to case, not in declaration order")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Sorted.slnx", R"(<Solution>
  <Folder Name="/Zebra/" />
  <Folder Name="/apple/" />
  <Project Path="z\Zulu.vcxproj" />
  <Project Path="a/alpha.vcxproj" />
</Solution>
)");

	const Core::Result<Solution> readResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(readResult.HasValue());

	const Folder& root = readResult.GetValue().Root;

	REQUIRE(root.Folders.size() == 2);
	CHECK(root.Folders[0].Name == "apple");
	CHECK(root.Folders[1].Name == "Zebra");

	REQUIRE(root.Projects.size() == 2);
	CHECK(root.Projects[0].DisplayName == "alpha");
	CHECK(root.Projects[1].DisplayName == "Zulu");
}

TEST_CASE("IsSolutionFile recognises both formats and nothing else, so a caller can refuse a file before re-rooting the workspace")
{
	CHECK(Graphite::IsSolutionFile("C:/work/App.slnx"));
	CHECK(Graphite::IsSolutionFile("C:/work/App.sln"));
	CHECK(Graphite::IsSolutionFile("C:/work/App.SLNX"));
	CHECK(Graphite::IsSolutionFile("C:/work/App.Sln"));

	CHECK_FALSE(Graphite::IsSolutionFile("C:/work/notes.txt"));
	CHECK_FALSE(Graphite::IsSolutionFile("C:/work/App.vcxproj"));
	CHECK_FALSE(Graphite::IsSolutionFile("C:/work/App"));
}
