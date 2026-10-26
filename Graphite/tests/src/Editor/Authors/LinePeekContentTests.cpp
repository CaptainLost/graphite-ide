#include <doctest/doctest.h>

#include "Editor/Authors/LinePeekContent.h"

#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"

#include <string>
#include <vector>

using Graphite::LineAuthors;
using Graphite::LineHistoryState;
using Graphite::LinePeekInput;
using Graphite::PeekDiffLine;
using Graphite::PeekState;

namespace {

	Core::Oid MakeOid(std::uint8_t byte)
	{
		Core::Oid id;
		id.Bytes.fill(byte);
		return id;
	}

	const Core::Oid kCommitA = MakeOid(0xA1);
	const Core::Oid kCommitB = MakeOid(0xB2);

	Core::Ref<const Graphite::HeadBlame> MakeHead(bool tracked = true)
	{
		auto blame = std::make_shared<Core::Blame>();
		blame->File = "a.txt";
		blame->LineCount = 3;
		blame->Hunks = {{1, 2, kCommitA, "a.txt", 1}, {3, 1, kCommitB, "a.txt", 5}};
		blame->Commits[kCommitA].Id = kCommitA;
		blame->Commits[kCommitB].Id = kCommitB;

		auto head = std::make_shared<Graphite::HeadBlame>();
		head->Tracked = tracked;
		head->Blame = std::move(blame);
		head->HeadText = "a\nb\nc\n";
		return head;
	}

	LineAuthors Lay(const Core::Ref<const Graphite::HeadBlame>& head, const std::string& buffer)
	{
		const auto hunks = Core::DiffTexts(head->HeadText, buffer);
		REQUIRE(hunks.HasValue());
		const Core::TextBuffer text{buffer};
		return Graphite::BuildLineAuthors(head, hunks.GetValue(), text.GetLineCount());
	}

	Core::LineChange MakeChange(const Core::Oid& id, std::size_t line)
	{
		Core::LineChange change;
		change.Commit.Id = id;
		change.Commit.Summary = "Summary";
		change.Commit.Author.Name = "Anna Kowalczyk";
		change.Path = "a.txt";
		change.Line = line;
		return change;
	}

	LineHistoryState Done(std::vector<Core::LineChange> changes, bool complete = true)
	{
		auto history = std::make_shared<Core::LineHistory>();
		history->Changes = std::move(changes);
		history->Complete = complete;
		LineHistoryState state;
		state.History = std::move(history);
		return state;
	}

	Graphite::LinePeekContent Build(const LineAuthors* authors, std::size_t line, std::string_view bufferLine,
									const LineHistoryState* history)
	{
		LinePeekInput input;
		input.Line = line;
		input.Authors = authors;
		input.BufferLine = bufferLine;
		input.History = history;
		return Graphite::BuildLinePeekContent(input);
	}

	bool SameLines(const std::vector<PeekDiffLine>& lines, const std::vector<PeekDiffLine>& expected)
	{
		if (lines.size() != expected.size())
		{
			return false;
		}

		for (std::size_t i = 0; i < lines.size(); ++i)
		{
			if (lines[i].Kind != expected[i].Kind || lines[i].Text != expected[i].Text ||
				lines[i].Tracked != expected[i].Tracked)
			{
				return false;
			}
		}

		return true;
	}
}

TEST_CASE("Peek content is loading until the authors arrive")
{
	const auto content = Build(nullptr, 0, "a", nullptr);

	CHECK(content.State == PeekState::Loading);
}

TEST_CASE("Peek content for a file without committed history fails with a message")
{
	const auto head = MakeHead(false);
	LineAuthors authors;
	authors.Head = head;

	const auto content = Build(&authors, 0, "a", nullptr);

	CHECK(content.State == PeekState::Failed);
	CHECK(content.Error == "This file has no committed history.");
}

TEST_CASE("Peek content lists an uncommitted edit first with the HEAD line it replaced")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nX\nc\n");
	const auto history = Done({MakeChange(kCommitA, 2)});

	const auto content = Build(&authors, 1, "X", &history);

	REQUIRE(content.State == PeekState::Ready);
	REQUIRE(content.Entries.size() == 2);
	CHECK(content.Entries[0].Title == "Uncommitted changes");
	CHECK(SameLines(content.Entries[0].Lines, {{Core::DiffLineKind::Removed, "b", true},
											   {Core::DiffLineKind::Added, "X", true}}));
	CHECK_FALSE(content.Entries[0].Commit.has_value());
	CHECK(content.Entries[1].Commit == std::optional<Core::Oid>{kCommitA});
}

TEST_CASE("Peek content for a line added since HEAD has one entry and needs no history")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nnew\nb\nc\n");

	const auto content = Build(&authors, 1, "new", nullptr);

	REQUIRE(content.State == PeekState::Ready);
	REQUIRE(content.Entries.size() == 1);
	CHECK(SameLines(content.Entries[0].Lines, {{Core::DiffLineKind::Added, "new", true}}));
}

TEST_CASE("Peek content marks the tracked line in each commit's hunks")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");

	Core::Hunk hunk;
	hunk.Lines = {{Core::DiffLineKind::Context, 1, 1, "a\n"},
				  {Core::DiffLineKind::Removed, 2, 0, "b\r\n"},
				  {Core::DiffLineKind::Added, 0, 2, "B\n"},
				  {Core::DiffLineKind::Context, 3, 3, "c\n"}};
	auto change = MakeChange(kCommitA, 2);
	change.ParentLine = 2;
	change.Hunks = {hunk};
	const auto history = Done({change});

	const auto content = Build(&authors, 1, "b", &history);

	REQUIRE(content.State == PeekState::Ready);
	REQUIRE(content.Entries.size() == 1);
	CHECK(SameLines(content.Entries[0].Lines, {{Core::DiffLineKind::Context, "a", false},
											   {Core::DiffLineKind::Removed, "b", true},
											   {Core::DiffLineKind::Added, "B", true},
											   {Core::DiffLineKind::Context, "c", false}}));
}

TEST_CASE("Peek content notes a history that stops early")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	const auto history = Done({MakeChange(kCommitA, 2)}, false);

	const auto content = Build(&authors, 1, "b", &history);

	CHECK(content.State == PeekState::Ready);
	CHECK(content.StopsEarly);
}

TEST_CASE("Peek content header counts the changes")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	const auto history = Done({MakeChange(kCommitA, 2), MakeChange(kCommitB, 2)});

	const auto content = Build(&authors, 2, "c", &history);

	CHECK(content.Header == "History of line 3 \xC2\xB7 2 changes \xC2\xB7 follows renames");
}
