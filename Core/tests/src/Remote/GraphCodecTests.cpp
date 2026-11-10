#include <doctest/doctest.h>

#include "Core/Remote/GraphCodec.h"

#include <string>
#include <vector>

namespace {

	const Core::Oid kFirst = *Core::Oid::FromHex("0123456789abcdef0123456789abcdef01234567");
	const Core::Oid kSecond = *Core::Oid::FromHex("89abcdef0123456789abcdef0123456789abcdef");
	const Core::Oid kThird = *Core::Oid::FromHex("fedcba9876543210fedcba9876543210fedcba98");
}

TEST_CASE("Graph commits round-trip through the binary payload, roots, merges, cut ones and odd keys included")
{
	const std::vector<Core::GraphCommit> commits{
		Core::GraphCommit{.Id = kFirst, .Time = 0, .AuthorKey = ""},
		Core::GraphCommit{.Id = kSecond, .Parents = {kFirst, kThird}, .Time = -5, .AuthorKey = "anna@example.com", .ParentsCut = true},
		Core::GraphCommit{.Id = kThird, .Parents = {kFirst}, .Time = 1700000000, .AuthorKey = "za\xC5\xBC\xC3\xB3\xC5\x82@example.com", .CommittedByAuthor = false},
	};

	const Core::Result<std::vector<Core::GraphCommit>> back = Core::DecodeGraphCommits(Core::EncodeGraphCommits(commits));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue() == commits);
}

TEST_CASE("No graph commits round-trip as none")
{
	const Core::Result<std::vector<Core::GraphCommit>> back = Core::DecodeGraphCommits(Core::EncodeGraphCommits({}));

	REQUIRE(back.HasValue());
	CHECK(back.GetValue().empty());
}

TEST_CASE("A graph payload cut short is refused")
{
	std::string bytes = Core::EncodeGraphCommits({Core::GraphCommit{.Id = kSecond, .Parents = {kFirst}, .AuthorKey = "anna@example.com"}});
	bytes.pop_back();

	const Core::Result<std::vector<Core::GraphCommit>> back = Core::DecodeGraphCommits(bytes);

	REQUIRE_FALSE(back.HasValue());
	CHECK(back.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("Another commit with one parent by an author already named costs at most 60 bytes")
{
	const Core::GraphCommit first{.Id = kSecond, .Parents = {kFirst}, .AuthorKey = "abcdefghij@example.c"};
	const Core::GraphCommit second{.Id = kThird, .Parents = {kSecond}, .AuthorKey = "abcdefghij@example.c"};

	const std::string one = Core::EncodeGraphCommits({first});
	const std::string two = Core::EncodeGraphCommits({first, second});

	CHECK(two.size() - one.size() <= 60);
}

TEST_CASE("A graph payload with bytes past its commits is refused")
{
	std::string bytes = Core::EncodeGraphCommits({Core::GraphCommit{.Id = kSecond, .AuthorKey = "a"}});
	bytes.push_back('x');

	CHECK_FALSE(Core::DecodeGraphCommits(bytes).HasValue());
}

TEST_CASE("Commit ids round-trip as twenty bytes each, and a ragged list is refused")
{
	const std::string bytes = Core::EncodeCommitIds({kFirst, kThird});

	const Core::Result<std::vector<Core::Oid>> back = Core::DecodeCommitIds(bytes);

	CHECK(bytes.size() == 40);
	REQUIRE(back.HasValue());
	CHECK(back.GetValue() == std::vector<Core::Oid>{kFirst, kThird});
	CHECK_FALSE(Core::DecodeCommitIds(bytes.substr(0, 39)).HasValue());
}
