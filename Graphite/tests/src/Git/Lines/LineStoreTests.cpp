#include <doctest/doctest.h>

#include "Git/Lines/LineStore.h"

#include "TempDirectory.h"

#include <cctype>
#include <string>
#include <vector>

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

TEST_CASE("Git window: the base order survives a save and a load")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "answers.json";

	Core::LineAnswers answers;
	answers.AddedBases = {"refs/heads/staging"};
	answers.KeptBases = {"refs/remotes/origin/release/2", "refs/remotes/origin/release/1"};
	answers.BaseOrder = {"refs/remotes/origin/release/2", "refs/heads/staging", "refs/remotes/origin/release/1"};

	REQUIRE(SaveLineAnswers(file, answers));
	CHECK(LoadLineAnswers(file).BaseOrder == answers.BaseOrder);
	CHECK(LoadLineAnswers(file) == answers);

	const std::filesystem::path written =
		directory.MakeFile("written.json", R"({"version": 1, "keptBases": ["refs/heads/x"], "baseOrder": ["refs/heads/x"]})");
	CHECK(LoadLineAnswers(written).BaseOrder == std::vector<std::string>{"refs/heads/x"});

	const std::filesystem::path withoutOrder = directory.MakeFile("without-order.json", R"({"version": 1, "keptBases": ["refs/heads/x"]})");
	const Core::LineAnswers loaded = LoadLineAnswers(withoutOrder);
	CHECK(loaded.KeptBases == std::vector<std::string>{"refs/heads/x"});
	CHECK(loaded.BaseOrder.empty());
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

TEST_CASE("Lines: a fixed directory gives a fixed folder name")
{
	// FNV-1a 64 of "c:/repo/.git", computed apart from the code under test.
	CHECK(LineStoreFor("C:/Repo/.git", "C:/Store").Answers.parent_path().filename() == "28b19bcbf6674468");
}

TEST_CASE("Lines: only an existing readable file of another version is foreign")
{
	TempDirectory directory;

	CHECK(HasForeignVersion(directory.GetPath() / "missing.json") == false);
	CHECK(HasForeignVersion(directory.MakeFile("malformed.json", "{not json")) == false);
	CHECK(HasForeignVersion(directory.MakeFile("two.json", R"({"version": 2})")) == true);
	CHECK(HasForeignVersion(directory.MakeFile("text.json", R"({"version": "x"})")) == true);

	const std::filesystem::path saved = directory.GetPath() / "saved.json";
	REQUIRE(SaveLineAnswers(saved, Core::LineAnswers{}));
	CHECK(HasForeignVersion(saved) == false);
}

TEST_CASE("Lines: two WSL repositories whose Linux paths differ only in case get two stores")
{
	const LineStorePaths lower = LineStoreFor(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\proj\\.git", "store");
	const LineStorePaths upper = LineStoreFor(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\Proj\\.git", "store");

	CHECK(lower.Graph != upper.Graph);
}

TEST_CASE("Lines: a WSL repository gives one store whatever the spelling of its share and distro")
{
	const LineStorePaths localhost = LineStoreFor(L"\\\\wsl.localhost\\Ubuntu\\home\\me\\proj\\.git", "store");
	const LineStorePaths dollar = LineStoreFor(L"\\\\wsl$\\UBUNTU\\home\\me\\proj\\.git\\\\", "store");

	CHECK(localhost.Graph == dollar.Graph);
}
