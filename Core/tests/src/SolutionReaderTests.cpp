#include <doctest/doctest.h>

#include "Core/Sln/SolutionReader.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>

using Core::Folder;
using Core::Project;
using Core::Solution;

namespace
{
    /// A .slnx as premake writes one: folders named with leading and trailing slashes,
    /// projects as self-closing children.
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

    const Folder* FindFolder(const Folder& parent, std::string_view name)
    {
        const auto match = std::find_if(parent.folders.begin(), parent.folders.end(),
                                        [name](const Folder& f) { return f.name == name; });

        return match == parent.folders.end() ? nullptr : &*match;
    }
}

TEST_CASE("Reading a solution that does not exist fails with NotFound")
{
    TempDirectory temp;

    const auto solution = Core::ReadSolution(temp.GetPath() / "missing.slnx");

    REQUIRE_FALSE(solution.HasValue());
    CHECK(solution.GetError().code == Core::ErrorCode::NotFound);
}

TEST_CASE("Reading a file with an unknown extension is unsupported")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("thing.cmake", "x");

    const auto solution = Core::ReadSolution(file);

    REQUIRE_FALSE(solution.HasValue());
    CHECK(solution.GetError().code == Core::ErrorCode::Unsupported);
}

TEST_CASE("Malformed XML reports an error rather than a partial solution")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Broken.slnx", "<Solution><Folder Name=");

    const auto solution = Core::ReadSolution(file);

    REQUIRE_FALSE(solution.HasValue());
    CHECK(solution.GetError().code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("The solution takes its name from the file")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("MyApp.slnx", kSampleSlnx);

    const auto solution = Core::ReadSolution(file);

    REQUIRE(solution.HasValue());
    CHECK(solution.GetValue().name == "MyApp");
    CHECK(solution.GetValue().path == file);
}

TEST_CASE("Solution folders become tree folders with the slashes stripped")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("MyApp.slnx", kSampleSlnx);

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());

    const Folder& root = solution.GetValue().root;
    REQUIRE(root.folders.size() == 2);

    // "/App/" in the file, "App" in the model — the slashes carry nesting, not the name.
    CHECK(FindFolder(root, "App") != nullptr);
    CHECK(FindFolder(root, "Engine") != nullptr);
}

TEST_CASE("Projects land in their folder with an absolute path")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("MyApp.slnx", kSampleSlnx);

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());

    const Folder* engine = FindFolder(solution.GetValue().root, "Engine");
    REQUIRE(engine != nullptr);
    REQUIRE(engine->projects.size() == 2);

    // The file spells paths relative to the solution and with backslashes; the model
    // holds absolute paths, because everything downstream opens them.
    const Project& core = engine->projects[0];
    CHECK(core.displayName == "Core");
    CHECK(core.path == temp.GetPath() / "Core" / "Core.vcxproj");
    CHECK(core.path.is_absolute());
}

TEST_CASE("Nested solution folders are nested in the model")
{
    TempDirectory temp;
    // The child is declared *before* its parent on purpose. EnsureFolder exists to make
    // that work, and declaring them in the easy order would leave that unproven.
    const auto file = temp.MakeFile("Nested.slnx", R"(<Solution>
  <Folder Name="/Group/Inner/">
    <Project Path="a\A.vcxproj" />
  </Folder>
  <Folder Name="/Group/" />
</Solution>
)");

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());

    // Nesting is expressed by the folder's path, not by element nesting — the two Folder
    // elements are siblings in the file.
    const Folder* group = FindFolder(solution.GetValue().root, "Group");
    REQUIRE(group != nullptr);
    REQUIRE(group->folders.size() == 1);
    CHECK(group->folders[0].name == "Inner");
    CHECK(group->folders[0].projects.size() == 1);
}

TEST_CASE("A project outside any folder sits at the solution root")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Flat.slnx",
                                    R"(<Solution><Project Path="a\A.vcxproj" /></Solution>)");

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());

    CHECK(solution.GetValue().root.folders.empty());
    REQUIRE(solution.GetValue().root.projects.size() == 1);
    CHECK(solution.GetValue().root.projects[0].displayName == "A");
}

TEST_CASE("An empty solution reads as empty rather than failing")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Empty.slnx", "<Solution />");

    const auto solution = Core::ReadSolution(file);

    REQUIRE(solution.HasValue());
    CHECK(solution.GetValue().projectCount == 0);
    CHECK(solution.GetValue().root.folders.empty());
    CHECK(solution.GetValue().root.projects.empty());
}

TEST_CASE("The project count covers every folder")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("MyApp.slnx", kSampleSlnx);

    const auto solution = Core::ReadSolution(file);

    REQUIRE(solution.HasValue());
    CHECK(solution.GetValue().projectCount == 3);
}

TEST_CASE("A project whose file is missing is marked rather than dropped")
{
    TempDirectory temp;
    temp.MakeFile("real/Real.vcxproj", "<Project />");
    const auto file = temp.MakeFile("Mixed.slnx", R"(<Solution>
  <Project Path="real\Real.vcxproj" />
  <Project Path="gone\Gone.vcxproj" />
</Solution>
)");

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());
    REQUIRE(solution.GetValue().root.projects.size() == 2);

    // Looked up by name rather than by position: the model is sorted, so declaration
    // order in the file says nothing about where a project lands.
    const auto& projects = solution.GetValue().root.projects;

    const auto real = std::find_if(projects.begin(), projects.end(),
                                   [](const Project& p) { return p.displayName == "Real"; });
    const auto gone = std::find_if(projects.begin(), projects.end(),
                                   [](const Project& p) { return p.displayName == "Gone"; });

    REQUIRE(real != projects.end());
    REQUIRE(gone != projects.end());

    // Both are listed — the solution says so, and that is the fact being reported.
    CHECK(real->exists);
    CHECK_FALSE(gone->exists);
}

TEST_CASE("Project contents are read by the solution reader")
{
    TempDirectory temp;

    // A real project file, with real contents, sitting exactly where the solution says.
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

    const auto file = temp.MakeFile("Real.slnx",
                                    R"(<Solution><Project Path="App\App.vcxproj" /></Solution>)");

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());
    REQUIRE(solution.GetValue().root.projects.size() == 1);

    const Project& project = solution.GetValue().root.projects[0];

    REQUIRE(project.exists);
    CHECK(project.contentRead);
    CHECK_FALSE(project.content.root.files.empty());
    CHECK(project.content.root.folders.empty());
    CHECK_FALSE(project.content.references.empty());
}

TEST_CASE("An uppercase extension is still a solution")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Shouty.SLNX", "<Solution />");

    // The filesystem does not care about the case, so neither may this.
    const auto solution = Core::ReadSolution(file);

    CHECK(solution.HasValue());
}

TEST_CASE("Valid XML that is not a solution is rejected")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Wrong.slnx", "<Project><ItemGroup /></Project>");

    // Parses perfectly well and means nothing here — worth its own branch, because the
    // failure otherwise looks like a corrupt file rather than the wrong one.
    const auto solution = Core::ReadSolution(file);

    REQUIRE_FALSE(solution.HasValue());
    CHECK(solution.GetError().code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Folders and projects come back sorted, not in declaration order")
{
    TempDirectory temp;
    const auto file = temp.MakeFile("Sorted.slnx", R"(<Solution>
  <Folder Name="/Zebra/" />
  <Folder Name="/apple/" />
  <Project Path="z\Zulu.vcxproj" />
  <Project Path="a/alpha.vcxproj" />
</Solution>
)");

    const auto solution = Core::ReadSolution(file);
    REQUIRE(solution.HasValue());

    const Folder& root = solution.GetValue().root;

    // Case-insensitive, so "apple" sits before "Zebra" rather than after it — the rule
    // the file tree already uses, and the one Visual Studio displays.
    REQUIRE(root.folders.size() == 2);
    CHECK(root.folders[0].name == "apple");
    CHECK(root.folders[1].name == "Zebra");

    REQUIRE(root.projects.size() == 2);
    CHECK(root.projects[0].displayName == "alpha");
    CHECK(root.projects[1].displayName == "Zulu");
}

TEST_CASE("IsSolutionFile recognises both formats and nothing else")
{
    // Exposed so a caller can refuse a file before rearranging itself around it. An
    // earlier version validated only inside ReadSolution, by which point the workspace
    // had already been re-rooted and the choice written to disk.
    CHECK(Core::IsSolutionFile("C:/work/App.slnx"));
    CHECK(Core::IsSolutionFile("C:/work/App.sln"));
    CHECK(Core::IsSolutionFile("C:/work/App.SLNX"));
    CHECK(Core::IsSolutionFile("C:/work/App.Sln"));

    CHECK_FALSE(Core::IsSolutionFile("C:/work/notes.txt"));
    CHECK_FALSE(Core::IsSolutionFile("C:/work/App.vcxproj"));
    CHECK_FALSE(Core::IsSolutionFile("C:/work/App"));
}
