#include <doctest/doctest.h>

#include "Search/SearchRequests.h"

#include "Core/Search/SearchQuery.h"

#include "GitFixture.h"

#include <algorithm>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

	Core::Result<Agent::GitAnswer> Call(const Agent::GitHandlers& handlers, const std::string& operation, json arguments)
	{
		const auto handler = handlers.find(operation);
		REQUIRE(handler != handlers.end());
		return handler->second(Core::AgentRequest{.Id = 1, .Operation = operation, .Arguments = std::move(arguments)});
	}

	std::vector<std::string> Listed(const std::string& payload)
	{
		std::vector<std::string> paths;
		std::size_t start = 0;

		while (start < payload.size())
		{
			const std::size_t end = payload.find('\0', start);
			paths.push_back(payload.substr(start, end - start));
			start = end == std::string::npos ? payload.size() : end + 1;
		}

		std::sort(paths.begin(), paths.end());
		return paths;
	}

	// A repository with a source file, an ignored folder, a binary and a large file.
	struct SearchTree
	{
		SearchTree()
		{
			Fixture.Write(".gitignore", "node_modules/\n");
			Fixture.Write("src/main.cpp", "int main()\n{\n\treturn answer;\n}\n");
			Fixture.Write("node_modules/lib.js", "answer\n");
			Fixture.Write("image.bin", std::string{"answer\0\x01", 8});
			Fixture.Write("notes.txt", "the answer is 42\n");
		}

		json At(json more = json::object()) const
		{
			more["root"] = Fixture.GetPath().string(); // not a path conversion: Linux paths are their bytes
			return more;
		}

		GitFixture Fixture;
	};
}

TEST_CASE("A folder's files are listed without the ignored folders and .git")
{
	SearchTree tree;
	const Agent::GitHandlers handlers = Agent::SearchHandlers();

	const Core::Result<Agent::GitAnswer> listed = Call(handlers, "files.list", tree.At({{"respectGitIgnore", true}}));

	REQUIRE(listed.HasValue());
	CHECK(Listed(listed.GetValue().Payload) == std::vector<std::string>{".gitignore", "image.bin", "notes.txt", "src/main.cpp"});
}

TEST_CASE("Without the ignore rules the ignored folders are listed too, .git never")
{
	SearchTree tree;
	const Agent::GitHandlers handlers = Agent::SearchHandlers();

	const Core::Result<Agent::GitAnswer> listed = Call(handlers, "files.list", tree.At({{"respectGitIgnore", false}}));

	REQUIRE(listed.HasValue());
	const std::vector<std::string> paths = Listed(listed.GetValue().Payload);
	CHECK(std::find(paths.begin(), paths.end(), "node_modules/lib.js") != paths.end());
	CHECK(std::none_of(paths.begin(), paths.end(), [](const std::string& path) { return path.starts_with(".git/"); }));
}

TEST_CASE("A search finds the text in the files git does not ignore, skipping binaries and the documents the host searches")
{
	SearchTree tree;
	const Agent::GitHandlers handlers = Agent::SearchHandlers();
	const json query{{"needle", "answer"}, {"mask", ""}, {"matchCase", false}, {"wholeWord", true}};

	const Core::Result<Agent::GitAnswer> found =
		Call(handlers, "search.text", tree.At({{"query", query}, {"respectGitIgnore", true}, {"skip", json::array({"notes.txt"})}}));

	REQUIRE(found.HasValue());
	const json hits = json::parse(found.GetValue().Payload);
	REQUIRE(hits.size() == 1);
	CHECK(hits[0].at("file") == "src/main.cpp");
	CHECK(hits[0].at("line") == 2);
	CHECK(hits[0].at("text") == "\treturn answer;");
	CHECK(found.GetValue().Result.at("skipped") == 1);
	CHECK(found.GetValue().Result.at("scanned") == 2);
}

TEST_CASE("A search stops before its hits outgrow a reply, and says it stopped")
{
	GitFixture fixture;
	// One line of about 1 MiB holding the needle 100 times: every hit carries the whole line.
	std::string line;

	for (int match = 0; match < 100; ++match)
	{
		line += "needle" + std::string(10 * 1024, 'x');
	}

	fixture.Write("dist/app.min.js", line + "\n");
	const json query{{"needle", "needle"}, {"mask", ""}, {"matchCase", true}, {"wholeWord", false}};

	const Core::Result<Agent::GitAnswer> found = Call(Agent::SearchHandlers(), "search.text",
		{{"root", fixture.GetPath().string()}, {"query", query}, {"respectGitIgnore", true}}); // not a path conversion

	REQUIRE(found.HasValue());
	CHECK(found.GetValue().Result.at("capped") == true);
	CHECK(found.GetValue().Payload.size() < 64u * 1024 * 1024 + 2u * 1024 * 1024);
	const json hits = json::parse(found.GetValue().Payload);
	CHECK(hits.size() > 0);
	CHECK(hits.size() < 100);
}
