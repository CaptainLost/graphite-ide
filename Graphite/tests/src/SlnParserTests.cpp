#include <doctest/doctest.h>

#include "Sln/SolutionReader.h"

#include "TempDirectory.h"

#include <algorithm>
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

	const Folder* FindFolder(const Folder& parent, std::string_view name)
	{
		const auto match = std::find_if(parent.folders.begin(), parent.folders.end(),
										[name](const Folder& f) { return f.name == name; });

		return match == parent.folders.end() ? nullptr : &*match;
	}

	const Project* FindProject(const Folder& parent, std::string_view name)
	{
		const auto match = std::find_if(parent.projects.begin(), parent.projects.end(),
										[name](const Project& p) { return p.displayName == name; });

		return match == parent.projects.end() ? nullptr : &*match;
	}
}

TEST_CASE("A .sln is read through the same entry point as a .slnx")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Legacy.sln", kSample);

	// The whole point of putting the parser behind ReadSolution: a caller offering both
	// formats writes one call.
	const auto solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().name == "Legacy");
	CHECK(solution.GetValue().projectCount == 2);
}

TEST_CASE("A folder type GUID makes a folder, not a project")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Legacy.sln", kSample);

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	// "App" is a Project entry in the file and a folder in the model — the type GUID is
	// the only thing that says so.
	CHECK(FindFolder(solution.GetValue().root, "App") != nullptr);
	CHECK(FindProject(solution.GetValue().root, "App") == nullptr);
}

TEST_CASE("NestedProjects decides where a project lands")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Legacy.sln", kSample);

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* app = FindFolder(solution.GetValue().root, "App");
	REQUIRE(app != nullptr);

	// Graphite is nested under App by the GUID pair; Core has no pair and stays at the
	// solution root.
	CHECK(FindProject(*app, "Graphite") != nullptr);
	CHECK(FindProject(solution.GetValue().root, "Core") != nullptr);
}

TEST_CASE("A parent declared after its child still adopts it")
{
	TempDirectory temp;

	// The nesting section names a parent whose entry comes later in the file. A
	// single-pass parser would have nowhere to put the child.
	const auto file = temp.MakeFile("Order.sln",
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

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* parent = FindFolder(solution.GetValue().root, "Parent");
	REQUIRE(parent != nullptr);
	CHECK(FindProject(*parent, "Child") != nullptr);
}

TEST_CASE("A folder nested inside another folder is nested in the model")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Deep.sln",
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

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Folder* outer = FindFolder(solution.GetValue().root, "Outer");
	REQUIRE(outer != nullptr);
	CHECK(FindFolder(*outer, "Inner") != nullptr);
}

TEST_CASE("A project path is made absolute against the solution")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Legacy.sln", kSample);

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* core = FindProject(solution.GetValue().root, "Core");
	REQUIRE(core != nullptr);

	// Spelled relative and with backslashes in the file; absolute in the model, as the
	// .slnx reader already guarantees.
	CHECK(core->path == temp.GetPath() / "Core" / "Core.vcxproj");
	CHECK(core->path.is_absolute());
}

TEST_CASE("The project type GUID is kept")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Legacy.sln", kSample);

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* core = FindProject(solution.GetValue().root, "Core");
	REQUIRE(core != nullptr);

	// Not displayed today, but it is what a writer will need to put the entry back, and
	// discarding it here would mean re-deriving it from the extension later.
	CHECK(core->typeId == "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}");
}

TEST_CASE("An empty .sln reads as empty rather than failing")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Bare.sln",
									"Microsoft Visual Studio Solution File, Format Version 12.00\n");

	const auto solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().projectCount == 0);
	CHECK(solution.GetValue().root.folders.empty());
}

TEST_CASE("A file that is not a solution at all is rejected")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Nonsense.sln", "this is not a solution file\n");

	// Recognised by its header line. Without that check any text file with the right
	// extension would read as an empty solution, which is a worse answer than an error.
	const auto solution = Graphite::ReadSolution(file);

	REQUIRE_FALSE(solution.HasValue());
	CHECK(solution.GetError().code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("A malformed entry line is skipped rather than failing the solution")
{
	TempDirectory temp;
	const auto file = temp.MakeFile("Partial.sln",
									"Microsoft Visual Studio Solution File, Format Version 12.00\n"
									"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = broken line with no quotes\n"
									"EndProject\n"
									"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Good\", \"g\\Good.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
									"EndProject\n");

	// Hand-edited and merge-conflicted solutions exist. One bad line should cost that
	// line, not the whole view.
	const auto solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().projectCount == 1);
	CHECK(FindProject(solution.GetValue().root, "Good") != nullptr);
}

TEST_CASE("Contents of C++ projects are filled in for a .sln too")
{
	TempDirectory temp;
	temp.MakeFile("g/Good.vcxproj", R"(<?xml version="1.0"?>
<Project><ItemGroup><ClCompile Include="Main.cpp" /></ItemGroup></Project>
)");

	const auto file = temp.MakeFile("Filled.sln",
									"Microsoft Visual Studio Solution File, Format Version 12.00\n"
									"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Good\", \"g\\Good.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
									"EndProject\n");

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());

	const Project* good = FindProject(solution.GetValue().root, "Good");
	REQUIRE(good != nullptr);

	// The reader registry and the sorting run after either parser, so both formats get
	// the same treatment from here on.
	CHECK(good->contentRead);
	REQUIRE(good->content.root.files.size() == 1);
	CHECK(good->content.root.files[0].name == "Main.cpp");
}

TEST_CASE("Two folders at the same level both keep their projects")
{
	TempDirectory temp;

	// The case that broke an earlier version of this parser. It held pointers into the
	// model while building it, and appending the second folder reallocated the vector the
	// first one lived in — so the project nested under the first wrote through a dangling
	// pointer. Every real solution has sibling folders; this one has three.
	const auto file = temp.MakeFile("Siblings.sln",
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

	const auto solution = Graphite::ReadSolution(file);
	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().projectCount == 2);

	const Folder* a = FindFolder(solution.GetValue().root, "A");
	const Folder* c = FindFolder(solution.GetValue().root, "C");

	REQUIRE(a != nullptr);
	REQUIRE(c != nullptr);
	CHECK(FindProject(*a, "InA") != nullptr);
	CHECK(FindProject(*c, "InC") != nullptr);
}

TEST_CASE("Two folders naming each other as parent do not hang")
{
	TempDirectory temp;

	// A hand-edited or badly merged solution can say this. Following the chain without a
	// limit recurses for ever; both folders belong at the root instead.
	const auto file = temp.MakeFile("Cycle.sln",
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

	const auto solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(FindFolder(solution.GetValue().root, "A") != nullptr);
	CHECK(FindFolder(solution.GetValue().root, "B") != nullptr);
}

TEST_CASE("A file written by Visual Studio is read despite its byte-order mark")
{
	TempDirectory temp;

	// Visual Studio writes a UTF-8 BOM and a blank line before the header. Demanding the
	// header on line one at column zero meant this parser could not open the only kind of
	// file it exists to open.
	const auto file = temp.MakeFile("Real.sln",
									"\xEF\xBB\xBF\n"
									"Microsoft Visual Studio Solution File, Format Version 12.00\n"
									"# Visual Studio Version 17\n"
									"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Only\", \"o\\Only.vcxproj\", \"{BBBB0001-0000-0000-0000-000000000000}\"\n"
									"EndProject\n");

	const auto solution = Graphite::ReadSolution(file);

	REQUIRE(solution.HasValue());
	CHECK(solution.GetValue().projectCount == 1);
}
