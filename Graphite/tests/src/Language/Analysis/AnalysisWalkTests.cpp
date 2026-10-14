#include <doctest/doctest.h>

#include "Language/Analysis/AnalysisWalk.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

	std::vector<std::string> AnalysableUnder(const std::filesystem::path& root, const std::filesystem::path& selectedSolution = {})
	{
		std::vector<std::string> names;

		Graphite::CollectAnalysable(root, selectedSolution, Core::CancellationToken{},
									[&names](const std::filesystem::path& file) { names.push_back(file.filename().string()); });

		std::sort(names.begin(), names.end());

		return names;
	}
}

TEST_CASE("Only files of a language with a server are worth queueing")
{
	TempDirectory temp;
	temp.MakeFile("a.cpp", "int x;");
	temp.MakeFile("b.h", "int y;");
	temp.MakeFile("notes.txt", "hello");
	temp.MakeFile("page.html", "<p>");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp", "b.h"});
}

TEST_CASE("A cancelled walk stops rather than finishing quietly")
{
	TempDirectory temp;

	for (int index = 0; index < 50; ++index)
	{
		temp.MakeFile("file" + std::to_string(index) + ".cpp", "int x;");
	}

	Core::CancellationToken token;
	std::size_t filesSeen = 0;

	Graphite::CollectAnalysable(temp.GetPath(), {}, token, [&](const std::filesystem::path&) {
		++filesSeen;
		token.Cancel();
	});

	CHECK(filesSeen == 1);
}

TEST_CASE("A nested project, whose files a different server than the workspace's would have to serve, is left to whoever serves it")
{
	TempDirectory temp;
	temp.MakeFile("a.cpp", "int x;");

	temp.MakeFile("vendor/imgui/.git", "gitdir: ../../.git/modules/imgui");
	temp.MakeFile("vendor/imgui/imgui.cpp", "int y;");
	temp.MakeFile("vendor/imgui/imgui.h", "int z;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp"});
}

TEST_CASE("A directory carrying its own compile database is a project of its own")
{
	TempDirectory temp;
	temp.MakeFile("a.cpp", "int x;");
	temp.MakeFile("embedded/.clangd", "CompileFlags: {}");
	temp.MakeFile("embedded/b.cpp", "int y;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp"});
}

TEST_CASE("A C++ file beside a solution in a subfolder is still analysed")
{
	TempDirectory temp;
	temp.MakeFile("main.cpp", "int x;");
	temp.MakeFile("sub/Tool.sln", "");
	temp.MakeFile("sub/a.cpp", "int y;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp", "main.cpp"});
}

TEST_CASE("The workspace's own markers do not exclude the workspace, which every repository carries at its root")
{
	TempDirectory temp;

	temp.MakeFile(".git/HEAD", "ref: refs/heads/main");
	temp.MakeFile("compile_commands.json", "[]");
	temp.MakeFile("a.cpp", "int x;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp"});
}

TEST_CASE("The walk reads a solution's language only under the selected solution and outside solutions nested below it")
{
	TempDirectory temp;
	temp.MakeFile("main.cpp", "int x;");
	temp.MakeFile("app/App.slnx", "");
	temp.MakeFile("app/Program.cs", "class Program { }");
	temp.MakeFile("app/samples/Sample.slnx", "");
	temp.MakeFile("app/samples/Sample.cs", "class Sample { }");
	temp.MakeFile("app/samples/sample.cpp", "int y;");
	temp.MakeFile("other/Other.cs", "class Other { }");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath(), temp.GetPath() / "app" / "App.slnx");

	CHECK(found == std::vector<std::string>{"Program.cs", "main.cpp", "sample.cpp"});
}

TEST_CASE("With no solution selected the walk reads the outermost solution folders and skips solutions nested below them")
{
	TempDirectory temp;
	temp.MakeFile("Loose.cs", "class Loose { }");
	temp.MakeFile("tool/Tool.sln", "");
	temp.MakeFile("tool/Tool.cs", "class Tool { }");
	temp.MakeFile("tool/tool.cpp", "int x;");
	temp.MakeFile("tool/samples/Sample.sln", "");
	temp.MakeFile("tool/samples/Sample.cs", "class Sample { }");
	temp.MakeFile("tool/samples/sample.cpp", "int y;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"Loose.cs", "Tool.cs", "sample.cpp", "tool.cpp"});
}
