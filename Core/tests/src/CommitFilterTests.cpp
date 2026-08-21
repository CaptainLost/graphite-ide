#include <doctest/doctest.h>

#include "Core/Git/CommitFilter.h"

#include <string>

using Core::CommitFilter;
using Core::CommitSummary;
using Core::Matches;
using Core::Oid;

namespace {

	Oid IdFrom(std::uint8_t first, std::uint8_t second)
	{
		Oid id;
		id.Bytes[0] = first;
		id.Bytes[1] = second;
		return id;
	}

	CommitSummary CommitWith(std::string summary, std::string body, std::string author,
							 std::string email)
	{
		CommitSummary commit;
		commit.Id = IdFrom(0xEB, 0x69);
		commit.Summary = std::move(summary);
		commit.Body = std::move(body);
		commit.Author.Name = std::move(author);
		commit.Author.Email = std::move(email);
		return commit;
	}

	CommitSummary Sample()
	{
		return CommitWith("fix: reset the kept width", "The rows stop being the same diff's.",
						  "Anna Kowalska", "anna@example.com");
	}
}

TEST_CASE("An empty filter accepts every commit")
{
	CHECK(Matches(CommitFilter{}, Sample()));
	CHECK(Matches(CommitFilter{}, CommitSummary{}));
}

TEST_CASE("A commit is matched by its summary")
{
	CHECK(Matches(CommitFilter{"kept width"}, Sample()));
}

TEST_CASE("A commit is matched by its body")
{
	// The body is where the reason for a change is written, so a search that reads only
	// the summary would miss exactly the commits worth finding.
	CHECK(Matches(CommitFilter{"same diff"}, Sample()));
}

TEST_CASE("A commit is matched by its author's name")
{
	CHECK(Matches(CommitFilter{"Kowalska"}, Sample()));
}

TEST_CASE("A commit is matched by its author's email")
{
	CHECK(Matches(CommitFilter{"anna@example"}, Sample()));
}

TEST_CASE("Matching ignores case on both sides")
{
	CHECK(Matches(CommitFilter{"KEPT WIDTH"}, Sample()));
	CHECK(Matches(CommitFilter{"kowalska"}, Sample()));
}

TEST_CASE("A commit nobody's text mentions is not matched")
{
	CHECK_FALSE(Matches(CommitFilter{"encoding"}, Sample()));
}

TEST_CASE("A commit is matched by a prefix of its id")
{
	// Typed straight off a build log or a review comment, where a commit is named by its
	// abbreviated hash and nothing else.
	CHECK(Matches(CommitFilter{"eb69"}, Sample()));
	CHECK(Matches(CommitFilter{"EB69"}, Sample()));
}

TEST_CASE("A hexadecimal text that is not a prefix of the id does not match it")
{
	// Prefix, not substring: that is how git itself resolves an abbreviated hash, and a
	// substring search over forty digits would match nearly anything short.
	CHECK_FALSE(Matches(CommitFilter{"69eb"}, Sample()));
}

TEST_CASE("A text that is not hexadecimal is never read as an id")
{
	CommitSummary commit = Sample();
	commit.Summary.clear();
	commit.Body.clear();
	commit.Author.Name.clear();
	commit.Author.Email.clear();

	CHECK_FALSE(Matches(CommitFilter{"zz"}, commit));
}

TEST_CASE("Surrounding whitespace is not part of the text")
{
	// A pasted hash arrives with a trailing space more often than not, and an id prefix is
	// the one match that cannot tolerate it.
	CHECK(Matches(CommitFilter{"  eb69 "}, Sample()));
	CHECK(Matches(CommitFilter{" kept width "}, Sample()));
}

TEST_CASE("A filter of nothing but whitespace accepts every commit")
{
	CHECK(Matches(CommitFilter{"   "}, Sample()));
}
