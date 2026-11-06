#include <doctest/doctest.h>

#include "Git/Lines/GraphFile.h"

#include "Core/FileSystem/AtomicWrite.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

	void PutLittleEndian(std::string& out, std::uint64_t value, int byteCount)
	{
		for (int index = 0; index < byteCount; ++index)
		{
			out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
		}
	}

	// A graph file written by hand: commit i has the id IdOf(i + 1), one author, no flags, and the parent indices
	// given. Version 1 differs from version 2 only in its magic and in having no committer flag.
	std::string GraphFileOf(const std::vector<std::vector<std::uint32_t>>& parentsPerCommit, std::string_view magic = "GLG2")
	{
		const std::string author = "ann@example.com";
		std::string bytes(magic);
		PutLittleEndian(bytes, 1, 4);
		PutLittleEndian(bytes, author.size(), 4);
		bytes += author;
		PutLittleEndian(bytes, parentsPerCommit.size(), 4);

		for (std::size_t commit = 0; commit < parentsPerCommit.size(); ++commit)
		{
			const Core::Oid id = IdOf(static_cast<std::uint8_t>(commit + 1));
			bytes.append(reinterpret_cast<const char*>(id.Bytes.data()), Core::Oid::kByteCount);
			PutLittleEndian(bytes, 100 + commit, 8);
			PutLittleEndian(bytes, 0, 4);
			PutLittleEndian(bytes, 0, 1);
			PutLittleEndian(bytes, parentsPerCommit[commit].size(), 1);

			for (const std::uint32_t parent : parentsPerCommit[commit])
			{
				PutLittleEndian(bytes, parent, 4);
			}
		}

		return bytes;
	}
}

TEST_CASE("Lines: a graph read children first is saved parents first and loads back")
{
	// The order a history walk returns: each commit before its parents.
	Core::GraphCommit root{IdOf(1), {}, 100, "ann@example.com", false};
	Core::GraphCommit left{IdOf(2), {IdOf(1)}, 200, "bob@example.com", false};
	Core::GraphCommit right{IdOf(3), {IdOf(1)}, 150, "ann@example.com", false};
	Core::GraphCommit merge{IdOf(4), {IdOf(2), IdOf(3)}, 300, "bob@example.com", false};
	Core::AncestryGraph graph;
	graph.Append({merge, right, left, root});

	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";
	REQUIRE(SaveGraph(file, graph).HasValue());

	const std::optional<Core::AncestryGraph> loaded = LoadGraph(file);
	REQUIRE(loaded.has_value());
	CHECK(Sorted(loaded->ToCommits()) == Sorted(graph.ToCommits()));
}

TEST_CASE("Lines: a graph file whose parent is not below its child is not trusted")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";

	REQUIRE(Core::WriteFileAtomically(file, GraphFileOf({{}, {0}, {0, 1}})).HasValue());
	CHECK(LoadGraph(file).has_value());

	SUBCASE("a commit that is its own parent")
	{
		REQUIRE(Core::WriteFileAtomically(file, GraphFileOf({{}, {1}})).HasValue());
		CHECK_FALSE(LoadGraph(file).has_value());
	}

	SUBCASE("two commits that are each other's parent")
	{
		REQUIRE(Core::WriteFileAtomically(file, GraphFileOf({{1}, {0}})).HasValue());
		CHECK_FALSE(LoadGraph(file).has_value());
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

TEST_CASE("Lines: work someone else committed stays told apart after saving and loading")
{
	Core::GraphCommit own{IdOf(1), {}, 100, "ann@example.com", false, true};
	Core::GraphCommit landed{IdOf(2), {IdOf(1)}, 200, "ann@example.com", false, false};
	Core::GraphCommit cutAndLanded{IdOf(3), {IdOf(99)}, 300, "bob@example.com", false, false};
	Core::AncestryGraph graph;
	graph.Append({own, landed, cutAndLanded});

	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";
	REQUIRE(SaveGraph(file, graph).HasValue());
	const std::optional<Core::AncestryGraph> loaded = LoadGraph(file);

	REQUIRE(loaded.has_value());
	CHECK(Sorted(loaded->ToCommits()) == Sorted(graph.ToCommits()));
	CHECK(loaded->CommittedByAuthor(loaded->Find(IdOf(1))));
	CHECK_FALSE(loaded->CommittedByAuthor(loaded->Find(IdOf(2))));
	CHECK_FALSE(loaded->CommittedByAuthor(loaded->Find(IdOf(3))));
	CHECK(loaded->ParentsCutAt(loaded->Find(IdOf(3))));
}

TEST_CASE("Lines: a graph file of the first version, which cannot tell who committed a commit, is read again")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "graph.bin";

	REQUIRE(Core::WriteFileAtomically(file, GraphFileOf({{}, {0}}, "GLG1")).HasValue());
	CHECK_FALSE(LoadGraph(file).has_value());

	REQUIRE(Core::WriteFileAtomically(file, GraphFileOf({{}, {0}})).HasValue());
	CHECK(LoadGraph(file).has_value());
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
		changed[3] = '9';
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
