#include <doctest/doctest.h>

#include "Solution/SolutionReader.h"

#include "SolutionFixtures.h"
#include "TempDirectory.h"

#include <string>

using Graphite::Folder;
using Graphite::Project;

namespace {
	const std::string kSample =
		"Microsoft Visual Studio Solution File, Format Version 12.00\n"
		"# Visual Studio Version 17\n"
		"Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"App\", \"App\", \"{AAAA0001-0000-0000-0000-000000000000}\"\n"
		"EndProject\n"
		"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Graphite\", \"Graphite\\Graphite.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
		"EndProject\n"
		"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Core\", \"Core\\Core.vcxproj\", \"{BBBB0002-0000-0000-0000-000000000000}\"\n"
		"EndProject\n"
		"Global\n"
		"\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
		"\t\tDebug|x64 = Debug|x64\n"
		"\tEndGlobalSection\n"
		"\tGlobalSection(NestedProjects) = preSolution\n"
		"\t\t{BBBB0001-0000-0000-0000-000000000000} = {AAAA0001-0000-0000-0000-000000000000}\n"
		"\tEndGlobalSection\n"
		"EndGlobal\n";

	const Project* FindProject(const Folder& parent, std::string_view name)
	{
		for (const Project& project : parent.Projects)
		{
			if (project.DisplayName == name)
			{
				return &project;
			}
		}

		return nullptr;
	}
}

TEST_CASE("A .sln is read through the same entry point as a .slnx")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Legacy.sln", kSample);

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().Name == "Legacy");
	CHECK(solution.GetValue().ProjectCount == 2);
}

TEST_CASE("A folder type GUID makes a folder, not a project")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Legacy.sln", kSample);

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	CHECK(FindChildFolder(solution.GetValue().Root, "App") != nullptr);
	CHECK(FindProject(solution.GetValue().Root, "App") == nullptr);
}

TEST_CASE("NestedProjects decides where a project lands")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Legacy.sln", kSample);

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* app = FindChildFolder(solution.GetValue().Root, "App");
	REQUIRE(app != nullptr);

	CHECK(FindProject(*app, "Graphite") != nullptr);
	CHECK(FindProject(solution.GetValue().Root, "Core") != nullptr);
}

TEST_CASE("A parent declared after its child still adopts it")
{
	TempDirectory temp;

	const std::filesystem::path file = temp.MakeFile("Order.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Child\", \"c\\Child.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Parent\", \"Parent\", \"{AAAA0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Global\n"
													 "\tGlobalSection(NestedProjects) = preSolution\n"
													 "\t\t{BBBB0001-0000-0000-0000-000000000000} = {AAAA0001-0000-0000-0000-000000000000}\n"
													 "\tEndGlobalSection\n"
													 "EndGlobal\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* parent = FindChildFolder(solution.GetValue().Root, "Parent");
	REQUIRE(parent != nullptr);
	CHECK(FindProject(*parent, "Child") != nullptr);
}

TEST_CASE("A folder nested inside another folder is nested in the model")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Deep.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Outer\", \"Outer\", \"{AAAA0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Inner\", \"Inner\", \"{AAAA0002-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Global\n"
													 "\tGlobalSection(NestedProjects) = preSolution\n"
													 "\t\t{AAAA0002-0000-0000-0000-000000000000} = {AAAA0001-0000-0000-0000-000000000000}\n"
													 "\tEndGlobalSection\n"
													 "EndGlobal\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* outer = FindChildFolder(solution.GetValue().Root, "Outer");
	REQUIRE(outer != nullptr);
	CHECK(FindChildFolder(*outer, "Inner") != nullptr);
}

TEST_CASE("A project path is made absolute against the solution")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Legacy.sln", kSample);

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* core = FindProject(solution.GetValue().Root, "Core");
	REQUIRE(core != nullptr);

	CHECK(core->Path == temp.GetPath() / "Core" / "Core.vcxproj");
	CHECK(core->Path.is_absolute());
}

TEST_CASE("The project type GUID is kept")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Legacy.sln", kSample);

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* core = FindProject(solution.GetValue().Root, "Core");
	REQUIRE(core != nullptr);

	CHECK(core->TypeId == "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}");
}

TEST_CASE("An empty .sln reads as empty rather than failing")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Bare.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().ProjectCount == 0);
	CHECK(solution.GetValue().Root.Folders.empty());
}

TEST_CASE("A file that is not a solution at all is rejected")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Nonsense.sln", "this is not a solution file\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE_FALSE(solution.HasValue());
	CHECK(solution.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("A malformed entry line is skipped rather than failing the solution")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("Partial.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = broken line with no quotes\n"
													 "EndProject\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Good\", \"g\\Good.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().ProjectCount == 1);
	CHECK(FindProject(solution.GetValue().Root, "Good") != nullptr);
}

TEST_CASE("Contents of C++ projects are filled in for a .sln too")
{
	TempDirectory temp;
	temp.MakeFile("g/Good.vcxproj", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="Main.cpp" /></ItemGroup></Project>
)");

	const std::filesystem::path file = temp.MakeFile("Filled.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Good\", \"g\\Good.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* good = FindProject(solution.GetValue().Root, "Good");
	REQUIRE(good != nullptr);

	CHECK(good->ContentRead);
	REQUIRE(good->Content.Root.Files.size() == 1);
	CHECK(good->Content.Root.Files[0].Name == "Main.cpp");
}

TEST_CASE("Two folders at the same level both keep their projects")
{
	TempDirectory temp;

	const std::filesystem::path file = temp.MakeFile("Siblings.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"A\", \"A\", \"{AAAA0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"B\", \"B\", \"{AAAA0002-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"C\", \"C\", \"{AAAA0003-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"InA\", \"a\\InA.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"InC\", \"c\\InC.vcxproj\", \"{BBBB0002-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Global\n"
													 "\tGlobalSection(NestedProjects) = preSolution\n"
													 "\t\t{BBBB0001-0000-0000-0000-000000000000} = {AAAA0001-0000-0000-0000-000000000000}\n"
													 "\t\t{BBBB0002-0000-0000-0000-000000000000} = {AAAA0003-0000-0000-0000-000000000000}\n"
													 "\tEndGlobalSection\n"
													 "EndGlobal\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().ProjectCount == 2);

	const Folder* firstFolder = FindChildFolder(solution.GetValue().Root, "A");
	const Folder* thirdFolder = FindChildFolder(solution.GetValue().Root, "C");

	REQUIRE(firstFolder != nullptr);
	REQUIRE(thirdFolder != nullptr);
	CHECK(FindProject(*firstFolder, "InA") != nullptr);
	CHECK(FindProject(*thirdFolder, "InC") != nullptr);
}

TEST_CASE("Two folders naming each other as parent do not hang")
{
	TempDirectory temp;

	const std::filesystem::path file = temp.MakeFile("Cycle.sln",
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"A\", \"A\", \"{AAAA0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"B\", \"B\", \"{AAAA0002-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n"
													 "Global\n"
													 "\tGlobalSection(NestedProjects) = preSolution\n"
													 "\t\t{AAAA0001-0000-0000-0000-000000000000} = {AAAA0002-0000-0000-0000-000000000000}\n"
													 "\t\t{AAAA0002-0000-0000-0000-000000000000} = {AAAA0001-0000-0000-0000-000000000000}\n"
													 "\tEndGlobalSection\n"
													 "EndGlobal\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(FindChildFolder(solution.GetValue().Root, "A") != nullptr);
	CHECK(FindChildFolder(solution.GetValue().Root, "B") != nullptr);
}

TEST_CASE("A file written by Visual Studio is read despite its byte-order mark")
{
	TempDirectory temp;

	const std::filesystem::path file = temp.MakeFile("Real.sln",
													 "\xEF\xBB\xBF\n"
													 "Microsoft Visual Studio Solution File, Format Version 12.00\n"
													 "# Visual Studio Version 17\n"
													 "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Only\", \"o\\Only.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
													 "EndProject\n");

	const Core::Result<Graphite::Solution> solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().ProjectCount == 1);
}
