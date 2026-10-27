#include <doctest/doctest.h>

#include "Editor/Authors/LinePeekContent.h"

#include "Core/Git/TextDiff.h"
#include "Core/Text/TextBuffer.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
				lines[i].Tracked != expected[i].Tracked || lines[i].Omitted != expected[i].Omitted)
			{
				return false;
			}
		}

		return true;
	}

	const PeekDiffLine kOmittedRow{.Kind = Core::DiffLineKind::Context, .Text = "\xE2\x80\xA6", .Omitted = true};

	std::string Numbered(std::string_view prefix, int number)
	{
		return std::string{prefix} + std::to_string(number);
	}

	// Ten lines of context, the tracked line replaced at line 11, then ten more lines of context.
	Core::Hunk ReplacedInTheMiddle()
	{
		Core::Hunk hunk;

		for (int line = 1; line <= 10; ++line)
		{
			hunk.Lines.push_back({Core::DiffLineKind::Context, line, line, Numbered("c", line) + "\n"});
		}

		hunk.Lines.push_back({Core::DiffLineKind::Removed, 11, 0, "old\n"});
		hunk.Lines.push_back({Core::DiffLineKind::Added, 0, 11, "new\n"});

		for (int line = 12; line <= 21; ++line)
		{
			hunk.Lines.push_back({Core::DiffLineKind::Context, line, line, Numbered("c", line) + "\n"});
		}

		return hunk;
	}

	// The commit that created a file of a hundred lines: one hunk, every line added.
	Core::Hunk WholeFileAdded()
	{
		Core::Hunk hunk;

		for (int line = 1; line <= 100; ++line)
		{
			hunk.Lines.push_back({Core::DiffLineKind::Added, 0, line, Numbered("a", line) + "\n"});
		}

		return hunk;
	}

	std::vector<PeekDiffLine> ShownLines(const Core::Hunk& hunk, std::size_t line, std::optional<std::size_t> parentLine)
	{
		const auto head = MakeHead();
		const auto authors = Lay(head, "a\nb\nc\n");

		auto change = MakeChange(kCommitA, line);
		change.ParentLine = parentLine;
		change.Hunks = {hunk};
		const auto history = Done({change});

		const auto content = Build(&authors, 1, "b", &history);
		REQUIRE(content.State == PeekState::Ready);
		REQUIRE(content.Entries.size() == 1);
		return content.Entries[0].Lines;
	}
}

TEST_CASE("Peek content is loading until the authors arrive")
{
	const auto content = Build(nullptr, 4, "a", nullptr);

	CHECK(content.State == PeekState::Loading);
	CHECK(content.Header == "History of line 5");
}

TEST_CASE("Peek content for a file without committed history fails with a message")
{
	const auto head = MakeHead(false);
	LineAuthors authors;
	authors.Head = head;

	const auto content = Build(&authors, 0, "a", nullptr);

	CHECK(content.State == PeekState::Failed);
	CHECK(content.Error == "This file has no committed history.");
	CHECK(content.Header == "History of line 1");
}

TEST_CASE("Peek content for an unusable head fails instead of loading")
{
	auto head = std::make_shared<Graphite::HeadBlame>();
	head->Tracked = true;
	head->Unusable = true;
	LineAuthors authors;
	authors.Head = head;

	const auto content = Build(&authors, 2, "a", nullptr);

	CHECK(content.State == PeekState::Failed);
	CHECK(content.Error == "This file has no committed history.");
	CHECK(content.Header == "History of line 3");
}

TEST_CASE("Peek content names the line while its history loads")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	const LineHistoryState history{.InFlight = true};

	const auto content = Build(&authors, 1, "b", &history);

	CHECK(content.State == PeekState::Loading);
	CHECK(content.Header == "History of line 2");
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

TEST_CASE("Peek content trims a hunk to the tracked line")
{
	const std::vector<PeekDiffLine> replaced = ShownLines(ReplacedInTheMiddle(), 11, 11);

	CHECK(SameLines(replaced, {kOmittedRow,
							   {Core::DiffLineKind::Context, "c8", false},
							   {Core::DiffLineKind::Context, "c9", false},
							   {Core::DiffLineKind::Context, "c10", false},
							   {Core::DiffLineKind::Removed, "old", true},
							   {Core::DiffLineKind::Added, "new", true},
							   {Core::DiffLineKind::Context, "c12", false},
							   {Core::DiffLineKind::Context, "c13", false},
							   {Core::DiffLineKind::Context, "c14", false},
							   kOmittedRow}));

	// A line untouched since the file was created: the hunk is the whole file.
	const std::vector<PeekDiffLine> created = ShownLines(WholeFileAdded(), 50, std::nullopt);

	REQUIRE(created.size() == 9);

	for (int offset = -3; offset <= 3; ++offset)
	{
		const PeekDiffLine& shown = created[static_cast<std::size_t>(offset + 4)];

		CHECK(shown.Text == Numbered("a", 50 + offset));
		CHECK(shown.Tracked == (offset == 0));
	}
}

TEST_CASE("Peek content marks the lines it left out")
{
	const std::vector<PeekDiffLine> created = ShownLines(WholeFileAdded(), 50, std::nullopt);

	REQUIRE(created.size() == 9);
	CHECK(SameLines({created.front()}, {kOmittedRow}));
	CHECK(SameLines({created.back()}, {kOmittedRow}));

	// The first line of the file: nothing above it is left out.
	const std::vector<PeekDiffLine> first = ShownLines(WholeFileAdded(), 1, std::nullopt);

	REQUIRE(first.size() == 5);
	CHECK(first.front().Text == "a1");
	CHECK_FALSE(first.front().Omitted);
	CHECK(SameLines({first.back()}, {kOmittedRow}));
}

TEST_CASE("Peek content builds the diff of the selected entry alone")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");

	auto first = MakeChange(kCommitA, 2);
	first.Hunks = {ReplacedInTheMiddle()};
	auto second = MakeChange(kCommitB, 2);
	second.Hunks = {WholeFileAdded()};
	const auto history = Done({first, second});

	LinePeekInput input;
	input.Line = 1;
	input.Selected = 1;
	input.Authors = &authors;
	input.BufferLine = "b";
	input.History = &history;

	const auto content = Graphite::BuildLinePeekContent(input);

	REQUIRE(content.Entries.size() == 2);
	CHECK(content.Entries[0].Lines.empty());
	CHECK_FALSE(content.Entries[1].Lines.empty());
}

TEST_CASE("Peek content cache builds again only when its source changes or its ages are a minute old")
{
	using namespace std::chrono_literals;

	Graphite::LinePeekCache cache;
	int builds = 0;
	const auto build = [&builds] {
		++builds;
		return Graphite::LinePeekContent{};
	};

	const std::chrono::sys_seconds start{std::chrono::seconds{1'800'000'000}};
	Graphite::LinePeekSource source{.Authors = std::make_shared<const LineAuthors>(), .Line = 3};

	cache.Get(source, start, build);
	cache.Get(source, start + 59s, build);
	CHECK(builds == 1);

	cache.Get(source, start + 60s, build);
	CHECK(builds == 2);

	source.Selected = 1;
	cache.Get(source, start + 60s, build);
	CHECK(builds == 3);

	source.History = std::make_shared<const Core::LineHistory>();
	cache.Get(source, start + 60s, build);
	CHECK(builds == 4);

	source.HistoryError = "walk failed";
	cache.Get(source, start + 60s, build);
	CHECK(builds == 5);

	source.Authors = std::make_shared<const LineAuthors>();
	cache.Get(source, start + 60s, build);
	cache.Get(source, start + 61s, build);
	CHECK(builds == 6);
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

TEST_CASE("Peek content for a line with no known author fails with a message")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	REQUIRE(authors.Lines.size() >= 4);
	REQUIRE(authors.Lines[3].Kind == Graphite::LineAuthorKind::Unknown);

	const auto content = Build(&authors, 3, "", nullptr);

	CHECK(content.State == PeekState::Failed);
	CHECK(content.Error == "This line has no committed history.");
}

TEST_CASE("Peek content fails with the message of a history error")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	LineHistoryState history;
	history.Error = Core::Error{};
	history.Error->Message = "walk failed";

	const auto content = Build(&authors, 1, "b", &history);

	CHECK(content.State == PeekState::Failed);
	CHECK(content.Error == "walk failed");
}

TEST_CASE("Peek content header counts the changes")
{
	const auto head = MakeHead();
	const auto authors = Lay(head, "a\nb\nc\n");
	const auto history = Done({MakeChange(kCommitA, 2), MakeChange(kCommitB, 2)});

	const auto content = Build(&authors, 2, "c", &history);

	CHECK(content.Header == "History of line 3 \xC2\xB7 2 changes \xC2\xB7 follows renames");
}
