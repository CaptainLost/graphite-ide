#include <doctest/doctest.h>

#include "Lang/Analysis/AnalysisWalk.h"

#include "TempDirectory.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

	std::vector<std::string> AnalysableUnder(const std::filesystem::path& root)
	{
		std::vector<std::string> names;

		Graphite::CollectAnalysable(root, Core::CancellationToken{},
									[&names](const std::filesystem::path& file)
									{ names.push_back(file.filename().string()); });

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

	Graphite::CollectAnalysable(temp.GetPath(), token, [&](const std::filesystem::path&)
	{
		++seen;
		token.Cancel();
	});

	CHECK(seen == 1);
}
