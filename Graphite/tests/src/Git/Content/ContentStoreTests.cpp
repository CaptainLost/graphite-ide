#include <doctest/doctest.h>

#include "Git/Content/ContentStore.h"
#include "Git/Lines/LineStore.h"

#include "TempDirectory.h"

#include <cstdint>
#include <string>

using namespace Graphite;

namespace {
	Core::Oid IdOf(std::uint8_t seed)
	{
		Core::Oid id;
		id.Bytes.fill(seed);
		return id;
	}

	Core::ContentVerdict SquashAt(std::uint8_t landing)
	{
		Core::ContentVerdict verdict;
		verdict.Outcome = Core::ContentOutcome::Merged;
		verdict.Kind = Core::ContentKind::Squash;
		verdict.Landing = IdOf(landing);
		return verdict;
	}

	Core::ContentVerdict RebaseOfTwo()
	{
		Core::ContentVerdict verdict;
		verdict.Outcome = Core::ContentOutcome::Merged;
		verdict.Kind = Core::ContentKind::Rebase;
		verdict.Twins = {{IdOf(20), IdOf(30)}, {IdOf(21), IdOf(31)}};
		return verdict;
	}

	const std::string kBranch = "0101010101010101010101010101010101010101";
	const std::string kLine = "0202020202020202020202020202020202020202";

	std::string OneEntryOfVersion(const std::string& version)
	{
		return R"({"version": )" + version + R"(, "verdicts": [{"branch": ")" + kBranch + R"(", "line": ")" + kLine
			   + R"(", "verdict": {"outcome": "notMerged"}}]})";
	}

	bool LoadsEmpty(TempDirectory& directory, const std::string& name, const std::string& text)
	{
		const ContentCache cache = LoadContentCache(directory.MakeFile(name, text));
		return cache.Verdicts.empty() && cache.Found.empty();
	}
}

TEST_CASE("Git window: the content cache survives a save and a load")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "sub" / "content.json";

	ContentCache saved;
	saved.Verdicts[{IdOf(1), IdOf(2)}] = SquashAt(9);
	saved.Verdicts[{IdOf(3), IdOf(2)}] = RebaseOfTwo();
	saved.Verdicts[{IdOf(4), IdOf(2)}] = Core::ContentVerdict{};
	saved.Found[IdOf(1)] = ContentFound{"refs/remotes/origin/main", SquashAt(9)};
	REQUIRE(SaveContentCache(file, saved));

	const ContentCache loaded = LoadContentCache(file);

	REQUIRE(loaded.Verdicts.size() == 3);
	CHECK(loaded.Verdicts.at({IdOf(1), IdOf(2)}) == SquashAt(9));
	CHECK(loaded.Verdicts.at({IdOf(3), IdOf(2)}) == RebaseOfTwo());
	CHECK(loaded.Verdicts.at({IdOf(4), IdOf(2)}).Outcome == Core::ContentOutcome::NotMerged);
	REQUIRE(loaded.Found.size() == 1);
	CHECK(loaded.Found.at(IdOf(1)).Line == "refs/remotes/origin/main");
	CHECK(loaded.Found.at(IdOf(1)).Verdict == SquashAt(9));
}

TEST_CASE("Git window: an unknown verdict is never stored")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "content.json";

	Core::ContentVerdict unknown;
	unknown.Outcome = Core::ContentOutcome::Unknown;

	ContentCache saved;
	saved.Verdicts[{IdOf(1), IdOf(2)}] = unknown;
	saved.Found[IdOf(1)] = ContentFound{"refs/heads/main", unknown};
	REQUIRE(SaveContentCache(file, saved));

	const ContentCache loaded = LoadContentCache(file);
	CHECK(loaded.Verdicts.empty());
	CHECK(loaded.Found.empty());
}

TEST_CASE("Git window: a damaged or newer content cache loads as empty")
{
	TempDirectory directory;

	CHECK(LoadContentCache(directory.MakeFile("current.json", OneEntryOfVersion("1"))).Verdicts.size() == 1);

	CHECK(LoadsEmpty(directory, "newer.json", OneEntryOfVersion("2")));
	CHECK(LoadsEmpty(directory, "malformed.json", "{not json"));
	CHECK(LoadsEmpty(directory, "array.json", "[]"));
	CHECK(LoadsEmpty(directory, "noversion.json", R"({"verdicts": []})"));
	CHECK(LoadContentCache(directory.GetPath() / "missing.json").Verdicts.empty());

	// A bad entry is dropped; the good one beside it is kept.
	const std::string mixed = R"({"version": 1, "verdicts": [
		{"branch": "zz", "line": "zz", "verdict": {"outcome": "merged"}},
		{"branch": ")" + kBranch + R"(", "line": ")" + kLine + R"(", "verdict": {"outcome": "merged", "kind": "squash", "landing": "nothex"}},
		{"branch": ")" + kBranch + R"(", "line": ")" + kLine + R"(", "verdict": {"outcome": "notMerged"}}]})";
	CHECK(LoadContentCache(directory.MakeFile("mixed.json", mixed)).Verdicts.size() == 1);
}

TEST_CASE("Git window: the content files sit beside the answers")
{
	const LineStorePaths paths = LineStoreFor("C:/Repo/.git", "C:/Store");

	CHECK(paths.Content.filename() == "content.json");
	CHECK(paths.Patches.filename() == "patches.bin");
	CHECK(paths.Content.parent_path() == paths.Answers.parent_path());
	CHECK(paths.Patches.parent_path() == paths.Answers.parent_path());
}
