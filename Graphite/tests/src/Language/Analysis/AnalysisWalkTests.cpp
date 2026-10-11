#include <doctest/doctest.h>

#include "Language/Analysis/AnalysisWalk.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

	std::vector<std::string> AnalysableUnder(const std::filesystem::path& root)
	{
		std::vector<std::string> names;

		Graphite::CollectAnalysable(root, Core::CancellationToken{},
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
	std::size_t seen = 0;

	Graphite::CollectAnalysable(temp.GetPath(), token, [&](const std::filesystem::path&) {
		++seen;
		token.Cancel();
	});

	CHECK(seen == 1);
}

TEST_CASE("A nested project is left to whoever serves it")
{
	TempDirectory temp;
	temp.MakeFile("a.cpp", "int x;");

	// What a submodule looks like: a .git FILE, not a directory. RootFor stops at it, so every
	// file below it belongs to a different server than the workspace's — one with no compile
	// database of its own, started only because this pass asked about code nobody opened.
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

TEST_CASE("The workspace's own markers do not exclude the workspace")
{
	TempDirectory temp;

	// Every repository this ever runs on has one of these at its root. Excluding a marked
	// directory without exempting the root would switch the whole feature off everywhere.
	temp.MakeFile(".git/HEAD", "ref: refs/heads/main");
	temp.MakeFile("compile_commands.json", "[]");
	temp.MakeFile("a.cpp", "int x;");

	const std::vector<std::string> found = AnalysableUnder(temp.GetPath());

	CHECK(found == std::vector<std::string>{"a.cpp"});
}
