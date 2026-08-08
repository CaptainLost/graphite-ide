#include <doctest/doctest.h>

#include "Core/Sln/CsprojReader.h"
#include "Core/Sln/SolutionReader.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>

using core::sln::ContentFolder;
using core::sln::CsprojReader;

namespace
{
    constexpr const char* kSdkProject = R"(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\Library\Library.csproj" />
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
                           [name](const core::sln::ContentFile& f) { return f.name == name; });
    }
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
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Program.cs", "x");
    temp.MakeFile("App/Startup.cs", "x");

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // The project file names no source at all — an SDK-style project globs them.
    CHECK(HasFile(content.Value().root, "Program.cs"));
    CHECK(HasFile(content.Value().root, "Startup.cs"));
}

TEST_CASE("Directories become folders")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Models/User.cs", "x");

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    const ContentFolder* models = FindFolder(content.Value().root, "Models");
    REQUIRE(models != nullptr);
    CHECK(HasFile(*models, "User.cs"));
}

TEST_CASE("Build output is left out")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Program.cs", "x");
    temp.MakeFile("App/bin/Debug/App.dll", "x");
    temp.MakeFile("App/obj/project.assets.json", "x");

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // Every .NET project has these and nobody wants to see them. The SDK excludes them
    // from the glob, so a reader that mirrors the SDK must too.
    CHECK(FindFolder(content.Value().root, "bin") == nullptr);
    CHECK(FindFolder(content.Value().root, "obj") == nullptr);
    CHECK(HasFile(content.Value().root, "Program.cs"));
}

TEST_CASE("The project file itself is not listed among its own contents")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Program.cs", "x");

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // Visual Studio shows the project as the node, not as a file inside itself.
    CHECK_FALSE(HasFile(content.Value().root, "App.csproj"));
}

TEST_CASE("Project references are read from the document")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    // The one thing this reader does take from the file rather than from the disk.
    REQUIRE(content.Value().references.size() == 1);
    CHECK(content.Value().references[0] == "Library");
}

TEST_CASE("A missing project file fails rather than reading as empty")
{
    TempDirectory temp;

    const auto content = CsprojReader{}.Read(temp.Path() / "Nothing.csproj");

    REQUIRE_FALSE(content.HasValue());
    CHECK(content.GetError().code == core::ErrorCode::NotFound);
}

TEST_CASE("Contents come back sorted")
{
    TempDirectory temp;
    const auto project = temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Zebra.cs", "x");
    temp.MakeFile("App/apple.cs", "x");

    const auto content = CsprojReader{}.Read(project);
    REQUIRE(content.HasValue());

    const auto& files = content.Value().root.files;
    REQUIRE(files.size() == 2);

    // The same case-insensitive order as everywhere else, so the two views cannot
    // disagree about where a name belongs.
    CHECK(files[0].name == "apple.cs");
    CHECK(files[1].name == "Zebra.cs");
}

TEST_CASE("A solution containing a C# project fills it in")
{
    TempDirectory temp;
    temp.MakeFile("App/App.csproj", kSdkProject);
    temp.MakeFile("App/Program.cs", "x");

    const auto file = temp.MakeFile("Mixed.slnx",
                                    R"(<Solution><Project Path="App\App.csproj" /></Solution>)");

    const auto solution = core::sln::ReadSolution(file);
    REQUIRE(solution.HasValue());
    REQUIRE(solution.Value().root.projects.size() == 1);

    // The registry picks the reader by extension, so adding a language is adding a file.
    CHECK(solution.Value().root.projects[0].contentRead);
    CHECK(HasFile(solution.Value().root.projects[0].content.root, "Program.cs"));
}
