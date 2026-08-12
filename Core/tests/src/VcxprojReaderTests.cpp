#include <doctest/doctest.h>

#include "Core/Sln/SolutionReader.h"
#include "Core/Sln/VcxprojReader.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>

using Core::ContentFolder;
using Core::ProjectContent;
using Core::VcxprojReader;

namespace
{
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

    const ContentFolder* FindFolder(const ContentFolder& parent, std::string_view name)
    {
        const auto match =
            std::find_if(parent.folders.begin(), parent.folders.end(),
                         [name](const ContentFolder& f) { return f.name == name; });

        return match == parent.folders.end() ? nullptr : &*match;
    }

    bool HasFile(const ContentFolder& folder, std::string_view name)
    {
        return std::any_of(folder.files.begin(), folder.files.end(),
                           [name](const Core::ContentFile& f) { return f.name == name; });
    }
}

TEST_CASE("The reader claims .vcxproj and nothing else")
{
    const VcxprojReader reader;

    CHECK(reader.Handles("C:/work/App.vcxproj"));
    CHECK_FALSE(reader.Handles("C:/work/App.csproj"));
    CHECK_FALSE(reader.Handles("C:/work/App.vcxproj.filters"));
}

TEST_CASE("Every item kind is read, and paths are made absolute")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", kProject);

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // ClInclude, ClCompile and None all appear; a file's path is relative to the project.
    CHECK(HasFile(content.Value().root, "App.h"));
    CHECK(HasFile(content.Value().root, "App.cpp"));
    CHECK(HasFile(content.Value().root, "notes.txt"));

    const auto& files = content.Value().root.files;
    const auto match = std::find_if(files.begin(), files.end(),
                                    [](const Core::ContentFile& f)
                                    { return f.name == "App.cpp"; });

    REQUIRE(match != files.end());
    CHECK(match->path == temp.Path() / "App" / "src" / "App.cpp");
}

TEST_CASE("Without a filters file every file sits at the project root")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", kProject);

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // What Visual Studio does with a project whose .filters is missing: the folder
    // structure is in that file, so without it there is none.
    CHECK(content.Value().root.folders.empty());
    CHECK(content.Value().root.files.size() == 4);
}

TEST_CASE("Filters become folders and claim their files")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", kProject);
    temp.MakeFile("App/App.vcxproj.filters", kFilters);

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    const ContentFolder* panels = FindFolder(content.Value().root, "Panels");
    REQUIRE(panels != nullptr);
    CHECK(HasFile(*panels, "Console.cpp"));

    // The other three have no filter entry, so they stay at the root.
    CHECK(content.Value().root.files.size() == 3);
    CHECK_FALSE(HasFile(content.Value().root, "Console.cpp"));
}

TEST_CASE("A nested filter becomes nested folders")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="a.cpp" /></ItemGroup></Project>
)");

    temp.MakeFile("App/App.vcxproj.filters", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="a.cpp"><Filter>Ui\Panels</Filter></ClCompile></ItemGroup></Project>
)");

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // Filters spell their nesting with backslashes inside one element.
    const ContentFolder* ui = FindFolder(content.Value().root, "Ui");
    REQUIRE(ui != nullptr);

    const ContentFolder* panels = FindFolder(*ui, "Panels");
    REQUIRE(panels != nullptr);
    CHECK(HasFile(*panels, "a.cpp"));
}

TEST_CASE("Project references are read separately from files")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", kProject);

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    REQUIRE(content.Value().references.size() == 1);
    CHECK(content.Value().references[0] == "Core");

    // A reference is not a file — it must not turn up in the tree as one.
    CHECK_FALSE(HasFile(content.Value().root, "Core.vcxproj"));
}

TEST_CASE("A missing project file fails rather than reading as empty")
{
    TempDirectory temp;

    const auto content = VcxprojReader{}.Read(temp.Path() / "Nothing.vcxproj");

    REQUIRE_FALSE(content.HasValue());
    CHECK(content.GetError().code == Core::ErrorCode::NotFound);
}

TEST_CASE("ReadSolution fills the contents of C++ projects it can find")
{
    TempDirectory temp;
    temp.MakeFile("App/App.vcxproj", kProject);
    const auto solution = temp.MakeFile("Sample.slnx",
                                        R"(<Solution><Project Path="App\App.vcxproj" /></Solution>)");

    const auto read = Core::ReadSolution(solution);
    REQUIRE(read.HasValue());
    REQUIRE(read.Value().root.projects.size() == 1);

    CHECK(read.Value().root.projects[0].contentRead);
    CHECK(HasFile(read.Value().root.projects[0].content.root, "App.cpp"));
}

TEST_CASE("A project of an unknown type stays unread without failing the solution")
{
    TempDirectory temp;
    temp.MakeFile("App/App.fsproj", "<Project />");
    const auto solution = temp.MakeFile("Sample.slnx",
                                        R"(<Solution><Project Path="App\App.fsproj" /></Solution>)");

    const auto read = Core::ReadSolution(solution);

    // The structure of the solution is still complete — only the inside is unknown.
    REQUIRE(read.HasValue());
    REQUIRE(read.Value().root.projects.size() == 1);
    CHECK_FALSE(read.Value().root.projects[0].contentRead);
    CHECK(read.Value().root.projects[0].displayName == "App");
}

TEST_CASE("A project file the solution names but disk does not have leaves the rest intact")
{
    TempDirectory temp;
    const auto solution = temp.MakeFile("Sample.slnx",
                                        R"(<Solution><Project Path="Gone\Gone.vcxproj" /></Solution>)");

    const auto read = Core::ReadSolution(solution);

    // Exactly the sort of problem this view exists to reveal, so it must not be fatal.
    REQUIRE(read.HasValue());
    REQUIRE(read.Value().root.projects.size() == 1);
    CHECK_FALSE(read.Value().root.projects[0].contentRead);
}

TEST_CASE("An uppercase project extension is still claimed")
{
    const VcxprojReader reader;

    // The filesystem is case-insensitive, so refusing App.VCXPROJ would render a
    // perfectly ordinary project as a leaf with no contents and no explanation.
    CHECK(reader.Handles("C:/work/App.VCXPROJ"));
    CHECK(reader.Handles("C:/work/App.VcxProj"));
}

TEST_CASE("Files come back sorted, mixing headers and sources")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.vcxproj", R"(<?xml version="1.0"?>
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

    const auto content = VcxprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    const auto& files = content.Value().root.files;
    REQUIRE(files.size() == 4);

    // A .vcxproj keeps every ClInclude in one group and every ClCompile in another, so
    // preserving the file's order would list all the headers above all the sources.
    // Visual Studio interleaves them by name and so must this.
    CHECK(files[0].name == "IdeLayer.cpp");
    CHECK(files[1].name == "IdeLayer.h");
    CHECK(files[2].name == "Main.cpp");
    CHECK(files[3].name == "Workspace.h");
}
