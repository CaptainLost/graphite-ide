#include <doctest/doctest.h>

#include "Git/Lines/GraphFile.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <algorithm>

using namespace Graphite;

namespace {
	Core::Oid IdOf(std::uint8_t seed)
	{
		Core::Oid id;
		id.Bytes.fill(seed);
		return id;
	}

	Core::AncestryGraph SampleGraph()
	{
		Core::GraphCommit root{IdOf(1), {}, 100, "ann@example.com", false};
		Core::GraphCommit left{IdOf(2), {IdOf(1)}, 200, "bob@example.com", false};
		Core::GraphCommit right{IdOf(3), {IdOf(1)}, 150, "ann@example.com", false};
		Core::GraphCommit merge{IdOf(4), {IdOf(2), IdOf(3)}, 300, "bob@example.com", false};
		Core::GraphCommit cut{IdOf(5), {IdOf(99)}, 50, "ann@example.com", false};

		Core::AncestryGraph graph;
		graph.Append({root, left, right, merge, cut});
		return graph;
	}

	std::vector<Core::GraphCommit> Sorted(std::vector<Core::GraphCommit> commits)
	{
		std::ranges::sort(commits, [](const Core::GraphCommit& a, const Core::GraphCommit& b) { return a.Id.Bytes < b.Id.Bytes; });
		return commits;
	}
}

TEST_CASE("Lines: a saved graph loads back equal")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";
	const Core::AncestryGraph graph = SampleGraph();

	REQUIRE(SaveGraph(file, graph).HasValue());
	const std::optional<Core::AncestryGraph> loaded = LoadGraph(file);

	REQUIRE(loaded.has_value());
	CHECK(Sorted(loaded->ToCommits()) == Sorted(graph.ToCommits()));
	CHECK(loaded->Size() == 5);
}

TEST_CASE("Lines: a damaged graph file is not trusted")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";
	const std::filesystem::path damaged = directory.GetPath() / "damaged.bin";

	CHECK_FALSE(LoadGraph(file).has_value());

	REQUIRE(SaveGraph(file, SampleGraph()).HasValue());
	const std::string bytes = Core::ReadFileBytes(file).GetValue();
	REQUIRE(LoadGraph(file).has_value());

	SUBCASE("cut in half")
	{
		REQUIRE(Core::WriteFileAtomically(damaged, bytes.substr(0, bytes.size() / 2)).HasValue());
		CHECK_FALSE(LoadGraph(damaged).has_value());
	}

	SUBCASE("another magic")
	{
		std::string changed = bytes;
		changed[3] = '2';
		REQUIRE(Core::WriteFileAtomically(damaged, changed).HasValue());
		CHECK_FALSE(LoadGraph(damaged).has_value());
	}

	SUBCASE("trailing bytes")
	{
		REQUIRE(Core::WriteFileAtomically(damaged, bytes + "x").HasValue());
		CHECK_FALSE(LoadGraph(damaged).has_value());
	}

	SUBCASE("a parent index past the end")
	{
		// The commit whose id is all 0x04 is the merge; after its id come time, author, flags and count, then two parents.
		std::string changed = bytes;
		const std::size_t at = changed.find(std::string(20, '\x04'));
		REQUIRE(at != std::string::npos);
		const std::size_t firstParent = at + 20 + 8 + 4 + 1 + 1;
		changed[firstParent] = 99;
		REQUIRE(Core::WriteFileAtomically(damaged, changed).HasValue());
		CHECK_FALSE(LoadGraph(damaged).has_value());
	}
}
