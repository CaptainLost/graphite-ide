#include <doctest/doctest.h>

#include "Git/Blame/LineAuthors.h"

#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"

#include <string>
#include <vector>

using Graphite::HeadBlame;
using Graphite::LineAuthorKind;
using Graphite::LineAuthors;

namespace {

	Core::Oid MakeOid(std::uint8_t byte)
	{
		Core::Oid id;
		id.Bytes.fill(byte);
		return id;
	}

	const Core::Oid kCommitA = MakeOid(0xA1);
	const Core::Oid kCommitB = MakeOid(0xB2);

	Core::Ref<const HeadBlame> MakeHead(std::string text, std::vector<Core::BlameHunk> hunks, std::size_t lineCount)
	{
		auto blame = std::make_shared<Core::Blame>();
		blame->File = "a.txt";
		blame->LineCount = lineCount;
		blame->Hunks = std::move(hunks);
		blame->Commits[kCommitA].Id = kCommitA;
		blame->Commits[kCommitB].Id = kCommitB;

		auto head = std::make_shared<HeadBlame>();
		head->Tracked = true;
		head->Blame = std::move(blame);
		head->HeadText = std::move(text);
		return head;
	}

	Core::Ref<const HeadBlame> MakeAbcHead()
	{
		return MakeHead("a\nb\nc\n",
						{{1, 2, kCommitA, "a.txt", 1}, {3, 1, kCommitB, "a.txt", 5}},
						3);
	}

	LineAuthors Lay(const Core::Ref<const HeadBlame>& head, const std::string& buffer)
	{
		const auto hunks = Core::DiffTexts(head->HeadText, buffer);
		REQUIRE(hunks.HasValue());
		const Core::TextBuffer text{buffer};
		return Graphite::BuildLineAuthors(head, hunks.GetValue(), text.GetLineCount());
	}
}

TEST_CASE("Line authors take the blame of unchanged lines")
{
	const auto authors = Lay(MakeAbcHead(), "a\nb\nc\n");

	REQUIRE(authors.Lines.size() >= 3);
	CHECK(authors.Lines[0].Kind == LineAuthorKind::Committed);
	CHECK(authors.Lines[0].Hunk == 0);
	CHECK(authors.Lines[0].HeadLine == std::optional<std::size_t>{1});
	CHECK(authors.Lines[2].Kind == LineAuthorKind::Committed);
	CHECK(authors.Lines[2].Hunk == 1);
	CHECK(authors.Lines[2].HeadLine == std::optional<std::size_t>{3});
}

TEST_CASE("Line authors mark an edited line uncommitted and keep the HEAD line it replaced")
{
	const auto authors = Lay(MakeAbcHead(), "a\nX\nc\n");

	REQUIRE(authors.Lines.size() >= 3);
	CHECK(authors.Lines[1].Kind == LineAuthorKind::Uncommitted);
	CHECK(authors.Lines[1].HeadLine == std::optional<std::size_t>{2});
	CHECK(authors.Lines[2].Kind == LineAuthorKind::Committed);
}

TEST_CASE("Line authors mark an added line uncommitted with no HEAD line")
{
	const auto authors = Lay(MakeAbcHead(), "a\nnew\nb\nc\n");

	REQUIRE(authors.Lines.size() >= 4);
	CHECK(authors.Lines[1].Kind == LineAuthorKind::Uncommitted);
	CHECK_FALSE(authors.Lines[1].HeadLine.has_value());
	CHECK(authors.Lines[2].Kind == LineAuthorKind::Committed);
	CHECK(authors.Lines[2].HeadLine == std::optional<std::size_t>{2});
}

TEST_CASE("Line authors pair a replaced block by position")
{
	const auto head = MakeHead("a\nb\nc\nd\n", {{1, 4, kCommitA, "a.txt", 1}}, 4);
	const auto authors = Lay(head, "a\nX\nY\nZ\nd\n");

	REQUIRE(authors.Lines.size() >= 5);
	CHECK(authors.Lines[1].Kind == LineAuthorKind::Uncommitted);
	CHECK(authors.Lines[1].HeadLine == std::optional<std::size_t>{2});
	CHECK(authors.Lines[2].HeadLine == std::optional<std::size_t>{3});
	CHECK_FALSE(authors.Lines[3].HeadLine.has_value());
	CHECK(authors.Lines[4].Kind == LineAuthorKind::Committed);
	CHECK(authors.Lines[4].HeadLine == std::optional<std::size_t>{4});
}

TEST_CASE("Line authors shift lines below a deletion")
{
	const auto authors = Lay(MakeAbcHead(), "a\nc\n");

	REQUIRE(authors.Lines.size() >= 2);
	CHECK(authors.Lines[1].Kind == LineAuthorKind::Committed);
	CHECK(authors.Lines[1].HeadLine == std::optional<std::size_t>{3});
	CHECK(authors.Lines[1].Hunk == 1);
}

TEST_CASE("Line authors give the empty line after a final newline no author")
{
	const auto authors = Lay(MakeAbcHead(), "a\nb\nc\n");

	REQUIRE(authors.Lines.size() == 4);
	CHECK(authors.Lines[3].Kind == LineAuthorKind::Unknown);
}

TEST_CASE("Line authors call lines appended past the end uncommitted")
{
	const auto authors = Lay(MakeAbcHead(), "a\nb\nc\nd\ne\n");

	REQUIRE(authors.Lines.size() >= 5);
	CHECK(authors.Lines[3].Kind == LineAuthorKind::Uncommitted);
	CHECK_FALSE(authors.Lines[3].HeadLine.has_value());
	CHECK(authors.Lines[4].Kind == LineAuthorKind::Uncommitted);
	CHECK_FALSE(authors.Lines[4].HeadLine.has_value());
}

TEST_CASE("Line authors start a line history at the commit that last changed the line")
{
	const auto authors = Lay(MakeAbcHead(), "a\nb\nc\n");

	const auto start = Graphite::LineHistoryStart(authors, 2);
	REQUIRE(start.has_value());
	CHECK(start->From == kCommitB);
	CHECK(start->File == std::filesystem::path{"a.txt"});
	CHECK(start->Line == 5);
}

TEST_CASE("Line authors start an uncommitted line's history at the HEAD line it replaced")
{
	const auto authors = Lay(MakeAbcHead(), "a\nX\nc\n");

	const auto start = Graphite::LineHistoryStart(authors, 1);
	REQUIRE(start.has_value());
	CHECK(start->From == kCommitA);
	CHECK(start->Line == 2);
}

TEST_CASE("Line authors give a line added since HEAD no history to start")
{
	const auto authors = Lay(MakeAbcHead(), "a\nnew\nb\nc\n");

	CHECK_FALSE(Graphite::LineHistoryStart(authors, 1).has_value());
}
