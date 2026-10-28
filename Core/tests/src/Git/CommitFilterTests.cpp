#include <doctest/doctest.h>

#include "Core/Git/CommitFilter.h"

#include <filesystem>
#include <string>
#include <string_view>

using Core::CommitFilter;
using Core::CommitMatchesFilter;
using Core::CommitSummary;
using Core::Oid;

namespace {

	constexpr std::uint8_t kSampleIdFirstByte = 0xEB;
	constexpr std::uint8_t kSampleIdSecondByte = 0x69;

	Oid IdStartingWith(std::uint8_t firstByte, std::uint8_t secondByte)
	{
		Oid id;
		id.Bytes[0] = firstByte;
		id.Bytes[1] = secondByte;
		return id;
	}

	CommitSummary CommitWith(std::string summary, std::string body, std::string author,
							 std::string email)
	{
		CommitSummary commit;
		commit.Id = IdStartingWith(kSampleIdFirstByte, kSampleIdSecondByte);
		commit.Summary = std::move(summary);
		commit.Body = std::move(body);
		commit.Author.Name = std::move(author);
		commit.Author.Email = std::move(email);
		return commit;
	}

	CommitSummary SampleCommit()
	{
		return CommitWith("fix: reset the kept width", "The rows stop being the same diff's.",
						  "Anna Kowalska", "anna@example.com");
	}
}

TEST_CASE("An empty filter accepts every commit")
{
	const CommitFilter emptyFilter{};
	const CommitSummary emptyCommit{};

	CHECK(CommitMatchesFilter(emptyFilter, SampleCommit()));
	CHECK(CommitMatchesFilter(emptyFilter, emptyCommit));
}

TEST_CASE("A commit is matched by its summary")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "kept width"}, SampleCommit()));
}

TEST_CASE("A commit is matched by its body")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "same diff"}, SampleCommit()));
}

TEST_CASE("A commit is matched by its author's name")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "Kowalska"}, SampleCommit()));
}

TEST_CASE("A commit is matched by its author's email")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "anna@example"}, SampleCommit()));
}

TEST_CASE("Matching ignores case on both sides")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "KEPT WIDTH"}, SampleCommit()));
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "kowalska"}, SampleCommit()));
}

TEST_CASE("A commit nobody's text mentions is not matched")
{
	CHECK_FALSE(CommitMatchesFilter(CommitFilter{.Text = "encoding"}, SampleCommit()));
}

TEST_CASE("A commit is matched by a prefix of its id")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "eb69"}, SampleCommit()));
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "EB69"}, SampleCommit()));
}

TEST_CASE("A hexadecimal text that is not a prefix of the id does not match it")
{
	CHECK_FALSE(CommitMatchesFilter(CommitFilter{.Text = "69eb"}, SampleCommit()));
}

TEST_CASE("A text that is not hexadecimal is never read as an id")
{
	CommitSummary commit = SampleCommit();
	commit.Summary.clear();
	commit.Body.clear();
	commit.Author.Name.clear();
	commit.Author.Email.clear();

	CHECK_FALSE(CommitMatchesFilter(CommitFilter{.Text = "zz"}, commit));
}

TEST_CASE("Surrounding whitespace is not part of the text, so a pasted id prefix still matches")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "  eb69 "}, SampleCommit()));
	CHECK(CommitMatchesFilter(CommitFilter{.Text = " kept width "}, SampleCommit()));
}

TEST_CASE("A filter of nothing but whitespace accepts every commit")
{
	CHECK(CommitMatchesFilter(CommitFilter{.Text = "   "}, SampleCommit()));
}

TEST_CASE("A pattern with no separator names the file, wherever it sits")
{
	CHECK(Core::NamesPath("Application.cpp", "Core/src/Core/App/Application.cpp"));
	CHECK(Core::NamesPath("*.cpp", "Core/src/Core/App/Application.cpp"));
	CHECK_FALSE(Core::NamesPath("Application.h", "Core/src/Core/App/Application.cpp"));
	CHECK_FALSE(Core::NamesPath("Application.cpp", "Core/src/Core/App/Application.h"));
}

TEST_CASE("A fragment of the file name names the file")
{
	CHECK(Core::NamesPath("Graph", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("Layer.cpp", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("graphitelayer", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK_FALSE(Core::NamesPath("Widget", "Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("A fragment is matched against the name, not against the directories above it")
{
	CHECK_FALSE(Core::NamesPath("src", "Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("A pattern with a wildcard is matched against the whole name")
{
	CHECK(Core::NamesPath("*Layer.cpp", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK(Core::NamesPath("Graph*", "Graphite/src/App/GraphiteLayer.cpp"));
	CHECK_FALSE(Core::NamesPath("Graph*", "Graphite/src/App/MyGraphThing.cpp"));
	CHECK_FALSE(Core::NamesPath("*.cpp", "Graphite/src/App/notes.md"));
}

TEST_CASE("A pattern with a separator is matched against the whole path, never a tail of it")
{
	CHECK(Core::NamesPath("Graphite/src/App/GraphiteLayer.cpp",
						  "Graphite/src/App/GraphiteLayer.cpp"));

	const std::string_view pathTail = "src/App/GraphiteLayer.cpp";
	CHECK_FALSE(Core::NamesPath(pathTail, "Graphite/src/App/GraphiteLayer.cpp"));
}

TEST_CASE("An empty pattern names nothing")
{
	const std::string_view emptyPattern = "";
	CHECK_FALSE(Core::NamesPath(emptyPattern, "Core/src/Core/App/Application.cpp"));
}

TEST_CASE("Root anchor names the root's file and not the same name deeper")
{
	CHECK(Core::NamesPath("/README.md", "README.md"));
	CHECK(Core::NamesPath("/readme.md", "README.md"));
	CHECK_FALSE(Core::NamesPath("/README.md", "docs/README.md"));
	CHECK_FALSE(Core::NamesPath("/README", "README.md"));
}

TEST_CASE("Root anchor with a directory names that path from the root")
{
	CHECK(Core::NamesPath("/src/a.cpp", "src/a.cpp"));
	CHECK_FALSE(Core::NamesPath("/src/a.cpp", "lib/src/a.cpp"));
	CHECK(Core::NamesPath("/*.cpp", "main.cpp"));
	CHECK_FALSE(Core::NamesPath("/*.cpp", "src/main.cpp"));
}

TEST_CASE("Root anchor alone names nothing")
{
	CHECK_FALSE(Core::NamesPath("/", "README.md"));
}

TEST_CASE("Root anchor is added to the pattern naming only a root file")
{
	CHECK(Core::PatternNamingOnly("README.md") == "/README.md");
	CHECK(Core::PatternNamingOnly(std::filesystem::path(L"src\\Core\\a.cpp")) == "src/Core/a.cpp");
	CHECK_FALSE(Core::NamesPath(Core::PatternNamingOnly("README.md"), "docs/README.md"));
}
