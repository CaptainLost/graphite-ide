#include <doctest/doctest.h>

#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineSource.h"
#include "Git/Lines/LineStore.h"

#include "Core/Git/Tag.h"

#include "GitFixture.h"
#include "TempDirectory.h"

#include <optional>
#include <vector>

using namespace Graphite;

namespace {

	// A local handle's answers, counting the questions about held commits and able to deny them.
	class CountingLineSource final : public LineSource
	{
	public:
		explicit CountingLineSource(const Core::Repository& repository) : m_Local(repository) {}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return m_Local.QueryBranches(); }
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return m_Local.QueryRemotes(); }
		Core::Result<std::unordered_set<Core::Oid>> ShallowBoundary() override { return m_Local.ShallowBoundary(); }

		Core::Result<std::vector<bool>> HoldsCommits(const std::vector<Core::Oid>& commits) override
		{
			Asked.push_back(commits);

			if (DenyEverything)
			{
				return std::vector<bool>(commits.size(), false);
			}

			return m_Local.HoldsCommits(commits);
		}

		Core::Result<std::vector<Core::GraphCommit>> ReadGraphCommits(const Core::GraphReadRequest& request) override
		{
			++GraphReads;

			if (IncrementalFailure && !request.Known.empty())
			{
				return *IncrementalFailure;
			}

			return m_Local.ReadGraphCommits(request);
		}

		std::vector<std::vector<Core::Oid>> Asked;
		bool DenyEverything = false;
		std::optional<Core::Error> IncrementalFailure;
		int GraphReads = 0;

	private:
		RepositoryLineSource m_Local;
	};

	struct TaggedHistory
	{
		TaggedHistory()
		{
			Fixture.Write("a.txt", "one\n");
			First = Fixture.Commit("first");
			Fixture.Write("a.txt", "two\n");
			Second = Fixture.Commit("second");
			Fixture.Write("a.txt", "three\n");
			Fixture.Commit("third");
			Fixture.TagLightweight("v1", First);
			Fixture.TagLightweight("v2", Second);

			// A commit only a tag reaches: its branch is left out of the reads' branch lists.
			Fixture.Branch("side");
			Fixture.Checkout("side");
			Fixture.Write("side.txt", "x\n");
			SideOnly = Fixture.Commit("side only");
			Fixture.TagLightweight("v3", SideOnly);
			Fixture.Checkout("master");
		}

		GitFixture Fixture;
		std::string First;
		std::string Second;
		std::string SideOnly;
	};

	LineReadInput InputFor(const Core::Repository& repository)
	{
		Core::TagCache cache;
		Core::Result<std::vector<Core::Branch>> branches = repository.QueryBranches();
		Core::Result<std::vector<Core::Tag>> tags = repository.QueryTags(cache);
		REQUIRE(branches.HasValue());
		REQUIRE(tags.HasValue());
		std::erase_if(branches.GetValue(), [](const Core::Branch& branch) { return branch.Name == "side"; });
		return LineReadInput{.Branches = Core::CreateRef<const std::vector<Core::Branch>>(std::move(branches).GetValue()),
							 .Tags = Core::CreateRef<const std::vector<Core::Tag>>(std::move(tags).GetValue()),
							 .Now = 1800000000};
	}
}

TEST_CASE("A read asks whether the tags' commits are held in one call")
{
	TaggedHistory history;
	TempDirectory store;
	const Core::Result<Core::Repository> repository = Core::Repository::Open(history.Fixture.GetPath());
	REQUIRE(repository.HasValue());
	CountingLineSource source{repository.GetValue()};

	const Core::Result<LineRead> read = ReadLines(source, LineStoreFor(repository.GetValue().CommonDirectory(), store.GetPath()), InputFor(repository.GetValue()));

	REQUIRE(read.HasValue());
	REQUIRE(source.Asked.size() == 1);
	CHECK(source.Asked[0].size() == 3);
	CHECK(read.GetValue().Graph->Find(*Core::Oid::FromHex(history.SideOnly)) != Core::AncestryGraph::kNoCommit);
}

TEST_CASE("A tag whose commit git no longer holds is left out of the read")
{
	TaggedHistory history;
	TempDirectory store;
	const Core::Result<Core::Repository> repository = Core::Repository::Open(history.Fixture.GetPath());
	REQUIRE(repository.HasValue());
	CountingLineSource source{repository.GetValue()};
	source.DenyEverything = true;

	const Core::Result<LineRead> read = ReadLines(source, LineStoreFor(repository.GetValue().CommonDirectory(), store.GetPath()), InputFor(repository.GetValue()));

	REQUIRE(read.HasValue());
	REQUIRE(read.GetValue().Graph != nullptr);
	CHECK(read.GetValue().Graph->Find(*Core::Oid::FromHex(history.SideOnly)) == Core::AncestryGraph::kNoCommit);
}

TEST_CASE("A read that times out or answers too much is not started again from nothing")
{
	TaggedHistory history;
	TempDirectory store;
	const Core::Result<Core::Repository> repository = Core::Repository::Open(history.Fixture.GetPath());
	REQUIRE(repository.HasValue());
	const LineStorePaths paths = LineStoreFor(repository.GetValue().CommonDirectory(), store.GetPath());
	CountingLineSource source{repository.GetValue()};
	const Core::Result<LineRead> first = ReadLines(source, paths, InputFor(repository.GetValue()));
	REQUIRE(first.HasValue());

	history.Fixture.Write("a.txt", "four\n");
	history.Fixture.Commit("fourth");
	const Core::Result<Core::Repository> again = Core::Repository::Open(history.Fixture.GetPath());
	REQUIRE(again.HasValue());
	CountingLineSource later{again.GetValue()};

	for (const Core::ErrorCode code : {Core::ErrorCode::Cancelled, Core::ErrorCode::TooLarge})
	{
		later.IncrementalFailure = Core::Error{code, "through the agent"};
		later.GraphReads = 0;
		LineReadInput input = InputFor(again.GetValue());
		input.Graph = first.GetValue().Graph;

		const Core::Result<LineRead> read = ReadLines(later, paths, input);

		REQUIRE_FALSE(read.HasValue());
		CHECK(read.GetError().Code == code);
		CHECK(later.GraphReads == 1);
	}
}

TEST_CASE("A local handle answers held commits in the order asked")
{
	TaggedHistory history;
	const Core::Result<Core::Repository> repository = Core::Repository::Open(history.Fixture.GetPath());
	REQUIRE(repository.HasValue());
	RepositoryLineSource source{repository.GetValue()};
	Core::Oid madeUp;
	madeUp.Bytes.fill(0xAB);

	const Core::Result<std::vector<bool>> held = source.HoldsCommits({*Core::Oid::FromHex(history.First), madeUp});

	REQUIRE(held.HasValue());
	CHECK(held.GetValue() == std::vector<bool>{true, false});
}
