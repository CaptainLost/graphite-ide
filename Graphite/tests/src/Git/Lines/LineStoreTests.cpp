#include <doctest/doctest.h>

#include "Git/Lines/LineStore.h"

#include "TempDirectory.h"

#include <cctype>

using namespace Graphite;

TEST_CASE("Lines: one repository gives one store folder however its directory is spelled")
{
	const std::filesystem::path root = "C:/Store";

	const LineStorePaths plain = LineStoreFor("C:/Repo/.git", root);
	const LineStorePaths backslashes = LineStoreFor("c:\\repo\\.git\\", root);
	const LineStorePaths dotDot = LineStoreFor("C:/Repo/sub/../.git", root);
	const LineStorePaths other = LineStoreFor("C:/Other/.git", root);

	CHECK(plain.Answers == backslashes.Answers);
	CHECK(plain.Answers == dotDot.Answers);
	CHECK(plain.Graph == backslashes.Graph);
	CHECK(plain.Answers != other.Answers);

	CHECK(plain.Answers.filename() == "answers.json");
	CHECK(plain.Graph.filename() == "graph.bin");
	CHECK(plain.Answers.parent_path() == plain.Graph.parent_path());
	CHECK(plain.Answers.parent_path().parent_path() == root);

	const std::string folder = plain.Answers.parent_path().filename().string(); // not a path
	CHECK(folder.size() == 16);

	for (const char digit : folder)
	{
		CHECK(std::isxdigit(static_cast<unsigned char>(digit)));
		CHECK(!std::isupper(static_cast<unsigned char>(digit)));
	}
}

TEST_CASE("Lines: answers survive a save and a load")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "nested" / "answers.json";

	Core::LineAnswers answers;
	answers.AddedBases = {"refs/remotes/origin/staging"};
	answers.RemovedBases = {"refs/heads/old"};
	answers.KeptBases = {"refs/remotes/origin/release/2"};
	answers.MainLine = std::nullopt;
	answers.Comparisons["refs/heads/feature/a"] = "refs/remotes/origin/release/2";
	answers.Comparisons["refs/heads/spike"] = std::nullopt;

	REQUIRE(SaveLineAnswers(file, answers));
	CHECK(LoadLineAnswers(file) == answers);

	answers.MainLine = "refs/heads/trunk";
	REQUIRE(SaveLineAnswers(file, answers));
	CHECK(LoadLineAnswers(file) == answers);
}

TEST_CASE("Lines: a missing or malformed or newer answers file reads as no answers")
{
	TempDirectory directory;

	CHECK(LoadLineAnswers(directory.GetPath() / "missing.json") == Core::LineAnswers{});

	const std::filesystem::path malformed = directory.MakeFile("malformed.json", "{not json");
	CHECK(LoadLineAnswers(malformed) == Core::LineAnswers{});

	const std::filesystem::path newer =
		directory.MakeFile("newer.json", R"({"version": 2, "addedBases": ["refs/heads/x"], "mainLine": "refs/heads/y"})");
	CHECK(LoadLineAnswers(newer) == Core::LineAnswers{});
}
