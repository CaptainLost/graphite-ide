#include <doctest/doctest.h>

#include "Core/Git/CommitFilter.h"

#include <filesystem>
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

TEST_CASE("A pattern with no separator names the file, wherever it sits")
{
	// What a reader types when they know the file and not the tree it lives in — which is
	// nearly always, since the tree is what they opened the history to be told.
	CHECK(Core::NamesPath("Application.cpp", "Core/src/Core/App/Application.cpp"));
	CHECK(Core::NamesPath("*.cpp", "Core/src/Core/App/Application.cpp"));
	CHECK_FALSE(Core::NamesPath("Application.h", "Core/src/Core/App/Application.cpp"));
	CHECK_FALSE(Core::NamesPath("Application.cpp", "Core/src/Core/App/Application.h"));
}

TEST_CASE("A fragment of the file name names the file")
{
	// What a reader types when they remember the beginning of a name and not the rest —
	// the same gesture that opens a file by typing three letters of it.
	CHECK(Core::NamesPath("Graph", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("Layer.cpp", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("graphitelayer", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK_FALSE(Core::NamesPath("Widget", "Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("A fragment is matched against the name, not against the directories above it")
{
	// Otherwise every file under Graphite/ would answer to "Graphite", and the box would
	// be a path search wearing a file search's clothes.
	CHECK_FALSE(Core::NamesPath("src", "Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("A pattern with a wildcard is matched against the whole name")
{
	// The wildcard is how a reader says where the loose part is, so honouring it as a
	// fragment too would make "*.cpp" mean nothing in particular.
	CHECK(Core::NamesPath("*Layer.cpp", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("Graph*", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK_FALSE(Core::NamesPath("Graph*", "Graphite/src/App/MyGraphThing.cpp"));
	CHECK_FALSE(Core::NamesPath("*.cpp", "Graphite/src/App/notes.md"));
}

TEST_CASE("A pattern with a separator is matched against the whole path")
{
	// The form the file and line histories pass, and the form a reader falls back to when
	// a bare name names more files than they meant.
	CHECK(Core::NamesPath("Graphite/src/App/GraphiteLayer.cpp",
						  "Graphite/src/App/GraphiteLayer.cpp"));

	// A tail of the path, not the whole of it. Whole rather than fragment here is what
	// keeps the file and line histories on the one file they were opened for: matching a
	// contained path would quietly widen both.
	CHECK_FALSE(Core::NamesPath("src/App/GraphiteLayer.cpp",
								"Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("An empty pattern names nothing")
{
	CHECK_FALSE(Core::NamesPath("", "Core/src/Core/App/Application.cpp"));
}
