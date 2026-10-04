#include <doctest/doctest.h>

#include "Core/ImGui/ToastText.h"

#include <string>
#include <string_view>
#include <vector>

namespace {
	using Core::LayoutToastText;
	using Core::ToastTextLine;
	using Core::ToastTextStyle;

	// One unit per byte in either face, so widths are character counts.
	float Monospace(std::string_view text, bool)
	{
		return static_cast<float>(text.size());
	}

	// What the line reads as: a gap between two runs is the space the layout left there.
	std::string TextOf(const ToastTextLine& line)
	{
		std::string text;
		float end = 0.0f;

		for (const Core::ToastTextRun& run : line.Runs)
		{
			if (!text.empty() && run.X > end + 0.001f)
			{
				text += ' ';
			}

			text += run.Text;
			end = run.X + run.Width;
		}

		return text;
	}

	std::vector<std::string> TextsOf(const std::vector<ToastTextLine>& lines)
	{
		std::vector<std::string> texts;

		for (const ToastTextLine& line : lines)
		{
			texts.push_back(TextOf(line));
		}

		return texts;
	}
}

TEST_CASE("A quoted name is a strong span and a path starts a line of its own")
{
	const auto lines = LayoutToastText("'side' is checked out in D:/Repos/IDE/.claude/worktrees/side", 40.0f, Monospace);

	REQUIRE(lines.size() == 2);
	CHECK(TextOf(lines[0]) == "'side' is checked out in");
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
	const auto lines = LayoutToastText(path, 20.0f, Monospace);

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
	const auto lines = LayoutToastText("C:\\x\\D--Repos-IDE\\y", 15.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"C:\\x\\", "D--Repos-IDE\\y"});
}

TEST_CASE("A path that fits stays whole, and the words after it follow on its line")
{
	const auto lines = LayoutToastText("see /tmp/a/b.txt for details", 30.0f, Monospace);

	REQUIRE(lines.size() == 2);
	CHECK(TextOf(lines[0]) == "see");
	CHECK(TextOf(lines[1]) == "/tmp/a/b.txt for details");
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
		const auto lines = LayoutToastText(std::string("x ") + std::string(path), 100.0f, Monospace);

		REQUIRE(lines.size() == 2);
		REQUIRE(lines[1].Runs.size() == 1);
		CHECK(lines[1].Runs[0].Style == ToastTextStyle::Path);
	}

	for (const std::string_view word : {"src/a.cpp", "and/or", "...", "a.b", ".hidden"})
	{
		const std::string shown(word);
		CAPTURE(shown);
		const auto lines = LayoutToastText(std::string("x ") + std::string(word), 100.0f, Monospace);

		REQUIRE(lines.size() == 1);
		CHECK(lines[0].Runs.back().Style == ToastTextStyle::Plain);
	}
}

TEST_CASE("Plain text wraps at spaces, and a line exactly the width fits")
{
	const auto lines = LayoutToastText("the quick brown fox jumps over the lazy dog", 15.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"the quick brown", "fox jumps over", "the lazy dog"});

	for (const ToastTextLine& line : lines)
	{
		REQUIRE(line.Runs.size() == 1);
		CHECK(line.Runs[0].X == 0.0f);
	}
}

TEST_CASE("A word longer than a line breaks anywhere")
{
	const auto lines = LayoutToastText("abcdefghijklmnopqrstuvwxyz", 10.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"abcdefghij", "klmnopqrst", "uvwxyz"});
}

TEST_CASE("A path segment longer than a line breaks anywhere, starting on the line it is on")
{
	const auto lines = LayoutToastText("/abcdefghijklmno/x", 10.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"/abcdefghi", "jklmno/x"});
}

TEST_CASE("A quoted span never breaks inside, spaces and all")
{
	const auto lines = LayoutToastText("branch 'my long name' here", 16.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"branch", "'my long name'", "here"});
	REQUIRE(lines.size() == 3);
	REQUIRE(lines[1].Runs.size() == 1);
	CHECK(lines[1].Runs[0].Style == ToastTextStyle::Strong);
}

TEST_CASE("Punctuation after a quoted span sits against it, in the plain face")
{
	const auto lines = LayoutToastText("branch 'side': gone", 100.0f, Monospace);

	REQUIRE(lines.size() == 1);
	CHECK(TextOf(lines[0]) == "branch 'side': gone");
	REQUIRE(lines[0].Runs.size() == 3);
	CHECK(lines[0].Runs[1].Text == "'side'");
	CHECK(lines[0].Runs[2].Text == ": gone");
	CHECK(lines[0].Runs[2].X == lines[0].Runs[1].X + lines[0].Runs[1].Width);
}

TEST_CASE("An apostrophe inside a word opens no strong span")
{
	const auto lines = LayoutToastText("don't stop, it's fine", 100.0f, Monospace);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 1);
	CHECK(lines[0].Runs[0].Style == ToastTextStyle::Plain);
	CHECK(lines[0].Runs[0].Text == "don't stop, it's fine");
}

TEST_CASE("A strong span is measured in the bold face")
{
	const auto wide = [](std::string_view text, bool bold) {
		return static_cast<float>(text.size()) * (bold ? 2.0f : 1.0f);
	};

	const auto lines = LayoutToastText("'ab' cd", 100.0f, wide);

	REQUIRE(lines.size() == 1);
	REQUIRE(lines[0].Runs.size() == 2);
	CHECK(lines[0].Runs[0].Width == 8.0f);
	CHECK(lines[0].Runs[1].X == 9.0f);
	CHECK(lines[0].Runs[1].Width == 2.0f);
}

TEST_CASE("A newline starts a new line and trailing whitespace adds none")
{
	const auto lines = LayoutToastText("first line\r\nsecond\n\nfourth\n", 100.0f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"first line", "second", "", "fourth"});
}

TEST_CASE("An empty message lays out no lines")
{
	CHECK(LayoutToastText("", 100.0f, Monospace).empty());
	CHECK(LayoutToastText("  \n ", 100.0f, Monospace).empty());
}

TEST_CASE("A line narrower than one character still makes progress")
{
	const auto lines = LayoutToastText("abc", 0.5f, Monospace);

	CHECK(TextsOf(lines) == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("A multi-byte character is never split")
{
	// A Polish word whose last four letters take two bytes each.
	const auto lines = LayoutToastText("za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87", 3.0f, Monospace);

	for (const ToastTextLine& line : lines)
	{
		for (const Core::ToastTextRun& run : line.Runs)
		{
			REQUIRE_FALSE(run.Text.empty());
			CHECK((static_cast<unsigned char>(run.Text.front()) & 0xC0) != 0x80);
		}
	}

	std::string joined;

	for (const std::string& text : TextsOf(lines))
	{
		joined += text;
	}

	CHECK(joined == "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87");
}
