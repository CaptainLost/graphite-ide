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

TEST_CASE("A commit with one parent and a short author key costs at most 80 bytes")
{
	const std::string empty = Core::EncodeGraphCommits({});
	const std::string one = Core::EncodeGraphCommits({Core::GraphCommit{.Id = kSecond, .Parents = {kFirst}, .AuthorKey = "abcdefghij@example.c"}});

	CHECK(one.size() - empty.size() <= 80);
}
