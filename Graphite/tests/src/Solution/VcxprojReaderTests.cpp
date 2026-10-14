#include <doctest/doctest.h>

#include "Solution/SolutionReader.h"
#include "Solution/VcxprojReader.h"

#include "SolutionFixtures.h"
#include "TempDirectory.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using Graphite::ContentFolder;
using Graphite::ProjectContent;
using Graphite::VcxprojReader;

namespace {
	constexpr const char* kProject = R"(<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClInclude Include="src\App.h" />
    <ClCompile Include="src\App.cpp" />
    <ClCompile Include="src\Panels\Console.cpp" />
    <None Include="notes.txt" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="..\Core\Core.vcxproj" />
  </ItemGroup>
</Project>
)";

	constexpr const char* kFilters = R"(<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <Filter Include="Panels" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="src\Panels\Console.cpp">
      <Filter>Panels</Filter>
    </ClCompile>
  </ItemGroup>
</Project>
)";

	const Graphite::ContentFile* FindFileNamed(const ContentFolder& folder, std::string_view name)
	{
		for (const Graphite::ContentFile& file : folder.Files)
		{
			if (file.Name == name)
			{
				return &file;
			}
		}

		return nullptr;
	}
}

TEST_CASE("The reader claims .vcxproj and nothing else")
{
	const VcxprojReader reader;

	CHECK(reader.Handles("C:/work/App.vcxproj"));
	CHECK_FALSE(reader.Handles("C:/work/App.csproj"));
	CHECK_FALSE(reader.Handles("C:/work/App.vcxproj.filters"));
}

TEST_CASE("Every item kind is read, and a file path relative to the project is made absolute")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", kProject);

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	CHECK(HasFileNamed(readResult.GetValue().Root, "App.h"));
	CHECK(HasFileNamed(readResult.GetValue().Root, "App.cpp"));
	CHECK(HasFileNamed(readResult.GetValue().Root, "notes.txt"));

	const Graphite::ContentFile* appSource = FindFileNamed(readResult.GetValue().Root, "App.cpp");

	REQUIRE(appSource != nullptr);
	CHECK(appSource->Path == temp.GetPath() / "App" / "src" / "App.cpp");
}

TEST_CASE("An item listed in the project but absent on disk reads as missing")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", kProject);
	temp.MakeFile("App/src/App.cpp", "int x;");

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	const ContentFolder& root = readResult.GetValue().Root;
	const Graphite::ContentFile* appSource = FindFileNamed(root, "App.cpp");
	const Graphite::ContentFile* appHeader = FindFileNamed(root, "App.h");
	const Graphite::ContentFile* notes = FindFileNamed(root, "notes.txt");

	REQUIRE(appSource != nullptr);
	REQUIRE(appHeader != nullptr);
	REQUIRE(notes != nullptr);

	CHECK(appSource->Exists);
	CHECK_FALSE(appHeader->Exists);
	CHECK_FALSE(notes->Exists);
}

TEST_CASE("Without a filters file every file sits at the project root, since the folder structure lives in that file")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", kProject);

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	CHECK(readResult.GetValue().Root.Folders.empty());
	CHECK(readResult.GetValue().Root.Files.size() == 4);
}

TEST_CASE("Filters become folders and claim their files")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", kProject);
	temp.MakeFile("App/App.vcxproj.filters", kFilters);

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	const ContentFolder* panels = FindChildFolder(readResult.GetValue().Root, "Panels");
	REQUIRE(panels != nullptr);
	CHECK(HasFileNamed(*panels, "Console.cpp"));

	CHECK(readResult.GetValue().Root.Files.size() == 3);
	CHECK_FALSE(HasFileNamed(readResult.GetValue().Root, "Console.cpp"));
}

TEST_CASE("A nested filter, spelled with backslashes in one element, becomes nested folders")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="a.cpp" /></ItemGroup></Project>
)");

	temp.MakeFile("App/App.vcxproj.filters", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="a.cpp"><Filter>Ui\Panels</Filter></ClCompile></ItemGroup></Project>
)");

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	const ContentFolder* ui = FindChildFolder(readResult.GetValue().Root, "Ui");
	REQUIRE(ui != nullptr);

	const ContentFolder* panels = FindChildFolder(*ui, "Panels");
	REQUIRE(panels != nullptr);
	CHECK(HasFileNamed(*panels, "a.cpp"));
}

TEST_CASE("Project references are read separately from files and never turn up in the tree as one")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", kProject);

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	REQUIRE(readResult.GetValue().References.size() == 1);
	CHECK(readResult.GetValue().References[0] == "Core");

	CHECK_FALSE(HasFileNamed(readResult.GetValue().Root, "Core.vcxproj"));
}

TEST_CASE("A missing project file fails rather than reading as empty")
{
	TempDirectory temp;

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(temp.GetPath() / "Nothing.vcxproj");

	REQUIRE_FALSE(readResult.HasValue());
	CHECK(readResult.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("ReadSolution fills the contents of C++ projects it can find")
{
	TempDirectory temp;
	temp.MakeFile("App/App.vcxproj", kProject);
	const std::filesystem::path solutionFile = temp.MakeFile("Sample.slnx",
															 R"(<Solution><Project Path="App\App.vcxproj" /></Solution>)");

	const Core::Result<Graphite::Solution> solutionResult = Graphite::ReadSolution(solutionFile);
	REQUIRE(solutionResult.HasValue());
	REQUIRE(solutionResult.GetValue().Root.Projects.size() == 1);

	CHECK(solutionResult.GetValue().Root.Projects[0].ContentRead);
	CHECK(HasFileNamed(solutionResult.GetValue().Root.Projects[0].Content.Root, "App.cpp"));
}

TEST_CASE("A project of an unknown type stays unread without failing the solution, whose structure is still complete")
{
	TempDirectory temp;
	temp.MakeFile("App/App.fsproj", "<Project />");
	const std::filesystem::path solutionFile = temp.MakeFile("Sample.slnx",
															 R"(<Solution><Project Path="App\App.fsproj" /></Solution>)");

	const Core::Result<Graphite::Solution> solutionResult = Graphite::ReadSolution(solutionFile);

	REQUIRE(solutionResult.HasValue());
	REQUIRE(solutionResult.GetValue().Root.Projects.size() == 1);
	CHECK_FALSE(solutionResult.GetValue().Root.Projects[0].ContentRead);
	CHECK(solutionResult.GetValue().Root.Projects[0].DisplayName == "App");
}

TEST_CASE("A project file the solution names but disk does not have is not fatal, because revealing it is the point of the view")
{
	TempDirectory temp;
	const std::filesystem::path solutionFile = temp.MakeFile("Sample.slnx",
															 R"(<Solution><Project Path="Gone\Gone.vcxproj" /></Solution>)");

	const Core::Result<Graphite::Solution> solutionResult = Graphite::ReadSolution(solutionFile);

	REQUIRE(solutionResult.HasValue());
	REQUIRE(solutionResult.GetValue().Root.Projects.size() == 1);
	CHECK_FALSE(solutionResult.GetValue().Root.Projects[0].ContentRead);
}

TEST_CASE("An uppercase project extension is still claimed because the filesystem ignores case")
{
	const VcxprojReader reader;

	CHECK(reader.Handles("C:/work/App.VCXPROJ"));
	CHECK(reader.Handles("C:/work/App.VcxProj"));
}

TEST_CASE("Files come back sorted by name, interleaving headers and sources instead of keeping each group together")
{
	TempDirectory temp;
	const std::filesystem::path projectFile = temp.MakeFile("App/App.vcxproj", R"(<?xml version="1.0"?>
<Project>
  <ItemGroup>
    <ClInclude Include="Workspace.h" />
    <ClInclude Include="IdeLayer.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="Main.cpp" />
    <ClCompile Include="IdeLayer.cpp" />
  </ItemGroup>
</Project>
)");

	const Core::Result<ProjectContent> readResult = VcxprojReader{}.Read(projectFile);
	REQUIRE(readResult.HasValue());

	const std::vector<Graphite::ContentFile>& files = readResult.GetValue().Root.Files;
	REQUIRE(files.size() == 4);

	CHECK(files[0].Name == "IdeLayer.cpp");
	CHECK(files[1].Name == "IdeLayer.h");
	CHECK(files[2].Name == "Main.cpp");
	CHECK(files[3].Name == "Workspace.h");
}
