#include <doctest/doctest.h>

#include "Solution/CsprojReader.h"
#include "Solution/SolutionReader.h"

#include "SolutionFixtures.h"
#include "TempDirectory.h"

#include <string>

using Graphite::ContentFolder;
using Graphite::CsprojReader;

namespace {
	constexpr const char* kSdkProject = R"(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\Library\Library.csproj" />
  </ItemGroup>
</Project>
)";
}

TEST_CASE("The reader claims .csproj and nothing else")
{
	const CsprojReader reader;

	CHECK(reader.Handles("C:/work/App.csproj"));
	CHECK(reader.Handles("C:/work/App.CSPROJ"));
	CHECK_FALSE(reader.Handles("C:/work/App.vcxproj"));
	CHECK_FALSE(reader.Handles("C:/work/App.fsproj"));
}

TEST_CASE("Files are found on disk, not in the project file")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Program.cs", "x");
	temp.MakeFile("App/Startup.cs", "x");

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	CHECK(HasFileNamed(content.GetValue().Root, "Program.cs"));
	CHECK(HasFileNamed(content.GetValue().Root, "Startup.cs"));

	for (const Graphite::ContentFile& file : content.GetValue().Root.Files)
	{
		CHECK(file.Exists);
	}
}

TEST_CASE("Directories become folders")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Models/User.cs", "x");

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	const ContentFolder* models = FindChildFolder(content.GetValue().Root, "Models");
	REQUIRE(models != nullptr);
	CHECK(HasFileNamed(*models, "User.cs"));
}

TEST_CASE("Build output is left out")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Program.cs", "x");
	temp.MakeFile("App/bin/Debug/App.dll", "x");
	temp.MakeFile("App/obj/project.assets.json", "x");

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	CHECK(FindChildFolder(content.GetValue().Root, "bin") == nullptr);
	CHECK(FindChildFolder(content.GetValue().Root, "obj") == nullptr);
	CHECK(HasFileNamed(content.GetValue().Root, "Program.cs"));
}

TEST_CASE("The project file itself is not listed among its own contents")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Program.cs", "x");

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	CHECK_FALSE(HasFileNamed(content.GetValue().Root, "App.csproj"));
}

TEST_CASE("Project references are read from the document")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	REQUIRE(content.GetValue().References.size() == 1);
	CHECK(content.GetValue().References[0] == "Library");
}

TEST_CASE("A missing project file fails rather than reading as empty")
{
	TempDirectory temp;

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(temp.GetPath() / "Nothing.csproj");

	REQUIRE_FALSE(content.HasValue());
	CHECK(content.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("Contents come back sorted")
{
	TempDirectory temp;
	const std::filesystem::path project = temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Zebra.cs", "x");
	temp.MakeFile("App/apple.cs", "x");

	const Core::Result<Graphite::ProjectContent> content = CsprojReader{}.Read(project);
	REQUIRE(content.HasValue());

	const std::vector<Graphite::ContentFile>& files = content.GetValue().Root.Files;
	REQUIRE(files.size() == 2);

	CHECK(files[0].Name == "apple.cs");
	CHECK(files[1].Name == "Zebra.cs");
}

TEST_CASE("A solution containing a C# project fills it in")
{
	TempDirectory temp;
	temp.MakeFile("App/App.csproj", kSdkProject);
	temp.MakeFile("App/Program.cs", "x");

	const std::filesystem::path file = temp.MakeFile("Mixed.slnx",
													 R"(<Solution><Project Path="App\App.csproj" /></Solution>)");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());
	REQUIRE(solution.GetValue().Root.Projects.size() == 1);

	CHECK(solution.GetValue().Root.Projects[0].ContentRead);
	CHECK(HasFileNamed(solution.GetValue().Root.Projects[0].Content.Root, "Program.cs"));
}
