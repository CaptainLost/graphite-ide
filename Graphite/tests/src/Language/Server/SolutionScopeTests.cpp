#include <doctest/doctest.h>

#include "Language/Server/SolutionScope.h"
#include "TempDirectory.h"

#include <filesystem>
#include <ostream>

TEST_CASE("A solution's folder is its scope and a folder below holding another solution is not")
{
	TempDirectory temp;
	temp.MakeFile("app/App.slnx", "");
	temp.MakeFile("app/samples/Sample.sln", "");
	Graphite::SolutionScope scope{temp.GetPath(), temp.GetPath() / "app" / "App.slnx"};

	CHECK(scope.GetFolder() == temp.GetPath() / "app");
	CHECK(scope.Includes(temp.GetPath() / "app" / "Program.cs"));
	CHECK(scope.Includes(temp.GetPath() / "app" / "src" / "deep" / "Model.cs"));
	CHECK_FALSE(scope.Includes(temp.GetPath() / "app" / "samples" / "Sample.cs"));
	CHECK_FALSE(scope.Includes(temp.GetPath() / "app" / "samples" / "nested" / "Deeper.cs"));
	CHECK_FALSE(scope.Includes(temp.GetPath() / "Outside.cs"));
}

TEST_CASE("Without a selected solution inside the root the root is the scope")
{
	TempDirectory temp;
	TempDirectory elsewhere;
	temp.MakeFile("tool/Tool.sln", "");

	const Graphite::SolutionScope outside{temp.GetPath(), elsewhere.GetPath() / "Other.slnx"};
	Graphite::SolutionScope scope{temp.GetPath(), {}};

	CHECK(outside.GetFolder() == temp.GetPath());
	CHECK(scope.GetFolder() == temp.GetPath());
	CHECK(scope.Includes(temp.GetPath() / "Loose.cs"));
	CHECK_FALSE(scope.Includes(temp.GetPath() / "tool" / "Tool.cs"));
}

TEST_CASE("Only a language whose servers root at a solution has a solution scope")
{
	CHECK(Graphite::LanguageRootsAtSolution(Graphite::LanguageId::CSharp));
	CHECK_FALSE(Graphite::LanguageRootsAtSolution(Graphite::LanguageId::Cpp));
	CHECK_FALSE(Graphite::LanguageRootsAtSolution(Graphite::LanguageId::PlainText));
}
