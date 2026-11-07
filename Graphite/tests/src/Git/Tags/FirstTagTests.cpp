#include <doctest/doctest.h>

#include "Git/Tags/FirstTag.h"

#include "GitFixtures.h"

#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Lines/LineQuestions.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	const Core::Oid kRoot = OidWithFirstByte(1);
	const Core::Oid kMiddle = OidWithFirstByte(2);
	const Core::Oid kTip = OidWithFirstByte(3);
	const Core::Oid kNotInGraph = OidWithFirstByte(99);

	constexpr std::int64_t kOlder = 1700000000;
	constexpr std::int64_t kNewer = 1750000000;

	// root - middle - tip.
	Core::Ref<const Core::AncestryGraph> Chain()
	{
		Core::AncestryGraph graph;
		graph.Append({Core::GraphCommit{.Id = kRoot, .Time = 100, .AuthorKey = "ann"},
					  Core::GraphCommit{.Id = kMiddle, .Parents = {kRoot}, .Time = 200, .AuthorKey = "ann"},
					  Core::GraphCommit{.Id = kTip, .Parents = {kMiddle}, .Time = 300, .AuthorKey = "ann"}});
		return Core::CreateRef<const Core::AncestryGraph>(std::move(graph));
	}

	Core::Tag TagOn(const std::string& name, const Core::Oid& commit, std::int64_t when)
	{
		return Core::Tag{.Name = name,
						 .FullName = "refs/tags/" + name,
						 .Target = commit,
						 .When = std::chrono::sys_seconds{std::chrono::seconds{when}}};
	}

	// A read of the chain with the tags given newest first, as the listing gives them, and their first-tag answer.
	LineRead ReadWith(std::vector<Core::Tag> tagsNewestFirst)
	{
		LineRead read;
		read.Graph = Chain();
		read.Tags = Core::CreateRef<const std::vector<Core::Tag>>(std::move(tagsNewestFirst));
		read.FirstTag = FirstTagsIn(*read.Graph, *read.Tags);
		return read;
	}

	// Empty when no tag holds the commit.
	std::string FirstTagNameOf(const LineRead& read, const Core::Oid& commit)
	{
		const Core::Tag* tag = FirstTagOf(read, commit);
		return tag != nullptr ? tag->Name : std::string{};
	}

	Core::Ref<const std::vector<Core::Tag>> Listing(std::vector<Core::Tag> tags)
	{
		return Core::CreateRef<const std::vector<Core::Tag>>(std::move(tags));
	}
}

TEST_CASE("First tag: a commit outside the read's graph has no tag")
{
	const LineRead read = ReadWith({TagOn("v2", kTip, kNewer), TagOn("v1", kMiddle, kOlder)});

	CHECK(FirstTagOf(read, kNotInGraph) == nullptr);

	CHECK(FirstTagNameOf(read, kRoot) == "v1");
	CHECK(FirstTagNameOf(read, kTip) == "v2");
}

TEST_CASE("First tag: an older tag on a later commit is the first tag of the commits below it")
{
	// The newer tag sits lower in the history than the older one, so the older holds everything the newer holds.
	const LineRead read = ReadWith({TagOn("newer", kMiddle, kNewer), TagOn("older", kTip, kOlder)});

	REQUIRE(read.FirstTag.size() == 3);
	CHECK(FirstTagNameOf(read, kRoot) == "older");
	CHECK(FirstTagNameOf(read, kMiddle) == "older");
	CHECK(FirstTagNameOf(read, kTip) == "older");
}

TEST_CASE("First tag: of two tags made in the same second the one whose name comes first holds the commit")
{
	// Newest first, ties by name, as the listing orders them.
	const LineRead read = ReadWith({TagOn("newest", kTip, kNewer), TagOn("alpha", kTip, kOlder), TagOn("beta", kTip, kOlder)});

	CHECK(FirstTagNameOf(read, kTip) == "alpha");
	REQUIRE(read.FirstTag.size() == 3);
	CHECK(read.FirstTag[read.Graph->Find(kRoot)] == 1);
}

TEST_CASE("First tag: a tag whose commit the graph lacks is passed over")
{
	const LineRead read = ReadWith({TagOn("lost", kNotInGraph, kOlder), TagOn("v1", kMiddle, kNewer)});

	REQUIRE(read.FirstTag.size() == 3);
	CHECK(FirstTagNameOf(read, kRoot) == "v1");
	CHECK(FirstTagOf(read, kTip) == nullptr);
	CHECK(read.FirstTag[read.Graph->Find(kTip)] == Core::kNoTag);
}

TEST_CASE("First tag: a read without tags holds no tag for any commit")
{
	LineRead read;
	read.Graph = Chain();

	CHECK(FirstTagOf(read, kRoot) == nullptr);

	const LineRead none = ReadWith({});
	CHECK(none.FirstTag == std::vector<std::uint32_t>(3, Core::kNoTag));
	CHECK(FirstTagOf(none, kRoot) == nullptr);
}

TEST_CASE("First tag: whether the repository has tags follows the newest listing known")
{
	const LineRead tagged = ReadWith({TagOn("v1", kMiddle, kOlder)});
	const LineRead untagged = ReadWith({});

	CHECK_FALSE(RepositoryHasTags(nullptr, nullptr));
	CHECK(RepositoryHasTags(nullptr, Listing({TagOn("v1", kMiddle, kOlder)})));
	CHECK_FALSE(RepositoryHasTags(nullptr, Listing({})));

	// Before the model's listing is read, the read's says it.
	CHECK(RepositoryHasTags(&tagged, nullptr));
	CHECK_FALSE(RepositoryHasTags(&untagged, nullptr));

	// A fetch that added or deleted every tag is known from the listing before the read follows it.
	CHECK(RepositoryHasTags(&untagged, Listing({TagOn("v1", kMiddle, kOlder)})));
	CHECK_FALSE(RepositoryHasTags(&tagged, Listing({})));
}
