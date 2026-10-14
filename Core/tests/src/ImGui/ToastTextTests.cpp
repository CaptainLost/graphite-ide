#include <doctest/doctest.h>

#include "Core/ImGui/ToastText.h"
#include "Core/Text/Encoding.h"

#include <string>
#include <string_view>
#include <vector>

namespace {
	constexpr float kRoomyLineWidth = 100.0f;
	constexpr float kGapTolerance = 0.001f;
	constexpr float kBoldWidthFactor = 2.0f;
	constexpr float kRegularWidthFactor = 1.0f;

	using Core::LayoutToastText;
	using Core::ToastTextLine;
	using Core::ToastTextStyle;

	float OneUnitPerByte(std::string_view text, bool)
	{
		return static_cast<float>(text.size());
	}

	std::string TextWithGapsAsSpaces(const ToastTextLine& line)
	{
		std::string text;
		float previousRunEnd = 0.0f;

		for (const Core::ToastTextRun& run : line.Runs)
		{
			if (!text.empty() && run.X > previousRunEnd + kGapTolerance)
			{
				text += ' ';
			}

			text += run.Text;
			previousRunEnd = run.X + run.Width;
		}

		return text;
	}

	float DoubleWidthWhenBold(std::string_view text, bool bold)
	{
		return static_cast<float>(text.size()) * (bold ? kBoldWidthFactor : kRegularWidthFactor);
	}

	std::vector<std::string> TextsOf(const std::vector<ToastTextLine>& lines)
	{
		std::vector<std::string> texts;

		for (const ToastTextLine& line : lines)
		{
			texts.push_back(TextWithGapsAsSpaces(line));
		}

		return texts;
	}
}

TEST_CASE("A quoted name is a strong span and a path starts a line of its own")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("'side' is checked out in D:/Repos/IDE/.claude/worktrees/side", 40.0f, OneUnitPerByte);

	REQUIRE(lines.size() == 2);
	CHECK(TextWithGapsAsSpaces(lines[0]) == "'side' is checked out in");
	REQUIRE(lines[0].Runs.size() == 2);
	CHECK(lines[0].Runs[0].Text == "'side'");
	CHECK(lines[0].Runs[0].Style == ToastTextStyle::Strong);
	CHECK(lines[0].Runs[1].Style == ToastTextStyle::Plain);

	REQUIRE(lines[1].Runs.size() == 1);
	CHECK(lines[1].Runs[0].Text == "D:/Repos/IDE/.claude/worktrees/side");
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Path);
	CHECK(lines[1].Runs[0].X == 0.0f);
}

TEST_CASE("A path too long for a line breaks only after a separator")
{
	const std::string path = "D:/Repos/IDE/.claude/projects/D--Repos-IDE/abc.jsonl";
	const std::vector<ToastTextLine> lines = LayoutToastText(path, 20.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"D:/Repos/IDE/", ".claude/projects/", "D--Repos-IDE/", "abc.jsonl"});

	for (const ToastTextLine& line : lines)
	{
		REQUIRE(line.Runs.size() == 1);
		CHECK(line.Runs[0].Style == ToastTextStyle::Path);
		CHECK(line.Runs[0].Width <= 20.0f);
	}
}

TEST_CASE("A path never breaks at a dash, even where that would fill the line")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("C:\\x\\D--Repos-IDE\\y", 15.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"C:\\x\\", "D--Repos-IDE\\y"});
}

TEST_CASE("A path that fits stays whole, and the words after it follow on its line")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("see /tmp/a/b.txt for details", 30.0f, OneUnitPerByte);

	REQUIRE(lines.size() == 2);
	CHECK(TextWithGapsAsSpaces(lines[0]) == "see");
	CHECK(TextWithGapsAsSpaces(lines[1]) == "/tmp/a/b.txt for details");
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Path);
	CHECK(lines[1].Runs[0].Text == "/tmp/a/b.txt");
	CHECK(lines[1].Runs.back().Style == ToastTextStyle::Plain);
}

TEST_CASE("Every kind of path start is recognised and anything else is plain")
{
	for (const std::string_view path : {"D:/a/b", "d:\\a\\b", "/a/b", "./a/b", "../a/b", "~/a/b", ".claude/a"})
	{
		const std::string shown(path);
		CAPTURE(shown);
		const std::vector<ToastTextLine> lines = LayoutToastText(std::string("x ") + std::string(path), kRoomyLineWidth, OneUnitPerByte);

		REQUIRE(lines.size() == 2);
		REQUIRE(lines[1].Runs.size() == 1);
		CHECK(lines[1].Runs[0].Style == ToastTextStyle::Path);
	}

	for (const std::string_view word : {"src/a.cpp", "and/or", "...", "a.b", ".hidden"})
	{
		const std::string shown(word);
		CAPTURE(shown);
		const std::vector<ToastTextLine> lines = LayoutToastText(std::string("x ") + std::string(word), kRoomyLineWidth, OneUnitPerByte);

		REQUIRE(lines.size() == 1);
		CHECK(lines[0].Runs.back().Style == ToastTextStyle::Plain);
	}
}

TEST_CASE("Plain text wraps at spaces, and a line exactly the width fits")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("the quick brown fox jumps over the lazy dog", 15.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"the quick brown", "fox jumps over", "the lazy dog"});

	for (const ToastTextLine& line : lines)
	{
		REQUIRE(line.Runs.size() == 1);
		CHECK(line.Runs[0].X == 0.0f);
	}
}

TEST_CASE("A word longer than a line breaks anywhere")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("abcdefghijklmnopqrstuvwxyz", 10.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"abcdefghij", "klmnopqrst", "uvwxyz"});
}

TEST_CASE("A path segment longer than a line breaks anywhere, starting on the line it is on")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("/abcdefghijklmno/x", 10.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"/abcdefghi", "jklmno/x"});
}

TEST_CASE("A quoted span never breaks inside, spaces and all")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("branch 'my long name' here", 16.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"branch", "'my long name'", "here"});
	REQUIRE(lines.size() == 3);
	REQUIRE(lines[1].Runs.size() == 1);
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Strong);
}

TEST_CASE("Punctuation after a quoted span sits against it, in the plain face")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("branch 'side': gone", kRoomyLineWidth, OneUnitPerByte);

	REQUIRE(lines.size() == 1);
	CHECK(TextWithGapsAsSpaces(lines[0]) == "branch 'side': gone");
	REQUIRE(lines[0].Runs.size() == 3);
	CHECK(lines[0].Runs[1].Text == "'side'");
	CHECK(lines[0].Runs[2].Text == ": gone");
	CHECK(lines[0].Runs[2].X == lines[0].Runs[1].X + lines[0].Runs[1].Width);
}

TEST_CASE("An apostrophe inside a word opens no strong span")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("don't stop, it's fine", kRoomyLineWidth, OneUnitPerByte);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 1);
	CHECK(lines[0].Runs[0].Style == ToastTextStyle::Plain);
	CHECK(lines[0].Runs[0].Text == "don't stop, it's fine");
}

TEST_CASE("A strong span is measured in the bold face")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("'ab' cd", kRoomyLineWidth, DoubleWidthWhenBold);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 2);
	CHECK(lines[0].Runs[0].Width == 8.0f);
	CHECK(lines[0].Runs[1].X == 9.0f);
	CHECK(lines[0].Runs[1].Width == 2.0f);
}

TEST_CASE("A newline starts a new line and trailing whitespace adds none")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("first line\r\nsecond\n\nfourth\n", kRoomyLineWidth, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"first line", "second", "", "fourth"});
}

TEST_CASE("An empty message lays out no lines")
{
	CHECK(LayoutToastText("", kRoomyLineWidth, OneUnitPerByte).empty());
	CHECK(LayoutToastText("  \n ", kRoomyLineWidth, OneUnitPerByte).empty());
}

TEST_CASE("A quoted path stays bold but starts its own line and breaks only after a separator")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("could not open 'C:/Repos/IDE/.claude/x.jsonl': denied", 20.0f, OneUnitPerByte);

	CHECK(TextsOf(lines) ==
		  std::vector<std::string>{"could not open", "'C:/Repos/IDE/", ".claude/x.jsonl':", "denied"});
	REQUIRE(lines.size() == 4);
	REQUIRE(lines[1].Runs.size() == 1);
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Strong);
	REQUIRE(lines[2].Runs.size() == 2);
	CHECK(lines[2].Runs[0].Style == ToastTextStyle::Strong);
	CHECK(lines[2].Runs[1].Text == ":");
}

TEST_CASE("A quoted path that fits a line still starts one of its own")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("open 'C:/a/b' now", kRoomyLineWidth, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"open", "'C:/a/b' now"});
	REQUIRE(lines.size() == 2);
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Strong);
}

TEST_CASE("A path in double quotes or brackets is a path")
{
	for (const std::string_view wrapped : {"\"C:/a/b\"", "(C:/a/b)", "\"./a/b\":", "(/a/b),"})
	{
		const std::string shown(wrapped);
		CAPTURE(shown);
		const std::vector<ToastTextLine> lines = LayoutToastText("open " + shown + " now", kRoomyLineWidth, OneUnitPerByte);

		REQUIRE(lines.size() == 2);
		CHECK(TextWithGapsAsSpaces(lines[1]) == shown + " now");
		CHECK(lines[1].Runs[0].Style == ToastTextStyle::Path);
		CHECK(lines[1].Runs[0].Text == shown);
	}
}

TEST_CASE("A separator on its own is not a path")
{
	for (const std::string_view message : {"A / B", "A \\ B", "A (/) B", "A // B"})
	{
		const std::string shown(message);
		CAPTURE(shown);
		const std::vector<ToastTextLine> lines = LayoutToastText(message, kRoomyLineWidth, OneUnitPerByte);

		REQUIRE(lines.size() == 1);
		REQUIRE(lines[0].Runs.size() == 1);
		CHECK(lines[0].Runs[0].Style == ToastTextStyle::Plain);
		CHECK(lines[0].Runs[0].Text == shown);
	}
}

TEST_CASE("A line narrower than one character still makes progress")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("abc", 0.5f, OneUnitPerByte);

	CHECK(TextsOf(lines) == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("A multi-byte character, such as the last four letters of a Polish word, is never split")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", 3.0f, OneUnitPerByte);

	for (const ToastTextLine& line : lines)
	{
		for (const Core::ToastTextRun& run : line.Runs)
		{
			REQUIRE_FALSE(run.Text.empty());
			CHECK_FALSE(Core::IsUtf8ContinuationByte(run.Text.front()));
		}
	}

	std::string joined;

	for (const std::string& text : TextsOf(lines))
	{
		joined += text;
	}

	CHECK(joined == "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
}

TEST_CASE("A letter outside ASCII is a letter, so an apostrophe after it opens no strong span")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("Å¼'a' b", kRoomyLineWidth, OneUnitPerByte);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 1);
	CHECK(lines[0].Runs[0].Style == ToastTextStyle::Plain);
	CHECK(lines[0].Runs[0].Text == "Å¼'a' b");
}

TEST_CASE("An apostrophe inside a quoted span does not close it, only one before a non-word does")
{
	const std::vector<ToastTextLine> lines = LayoutToastText("'it's' ok", kRoomyLineWidth, OneUnitPerByte);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 2);
	CHECK(lines[0].Runs[0].Text == "'it's'");
	CHECK(lines[0].Runs[0].Style == ToastTextStyle::Strong);
}
