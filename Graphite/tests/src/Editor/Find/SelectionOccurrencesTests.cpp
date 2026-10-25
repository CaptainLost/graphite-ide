#include <doctest/doctest.h>

#include "Editor/Find/SelectionOccurrences.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

namespace {

	constexpr bool kEnabled = true;
	constexpr bool kDisabled = false;

	constexpr float kLinesOnTrack = 100.0f;

	float LineTopOfAHundred(std::size_t line)
	{
		return static_cast<float>(line) / kLinesOnTrack;
	}

	std::vector<std::size_t> StartsOf(const Graphite::SelectionOccurrences& found)
	{
		std::vector<std::size_t> starts;

		for (const Core::FindMatch& match : found.Matches)
		{
			starts.push_back(match.From);
		}

		return starts;
	}

	Core::OffsetRange Covering(const std::string& text, const std::string& word, std::size_t searchFrom = 0)
	{
		const std::size_t from = text.find(word, searchFrom);
		REQUIRE(from != std::string::npos);

		return Core::OffsetRange{.From = from, .To = from + word.size()};
	}

	Core::OpenItem ItemSelecting(Core::OffsetRange selection)
	{
		Core::OpenItem item;
		item.Caret = Core::Caret{.Offset = selection.To, .Anchor = selection.From};

		return item;
	}
}

TEST_CASE("A selected word is found wherever it stands whole and in the same case")
{
	const std::string text = "_steam = Open(_steamReader);\nthis._steam.Close(_Steam);\nreturn _steam;";
	const Core::TextBuffer buffer(text);

	const Graphite::SelectionOccurrences found = Graphite::FindSelectionOccurrences(buffer, Covering(text, "_steam"));

	CHECK(StartsOf(found) == std::vector<std::size_t>{0, text.find("_steam."), text.find("_steam;")});

	for (const Core::FindMatch& match : found.Matches)
	{
		CHECK(match.To - match.From == std::string("_steam").size());
	}
}

TEST_CASE("The lines holding a selected word are listed once each, in order")
{
	const std::string text = "a = a;\nb;\nreturn a;";
	const Core::TextBuffer buffer(text);

	const Graphite::SelectionOccurrences found = Graphite::FindSelectionOccurrences(buffer, Covering(text, "a"));

	CHECK(found.Matches.size() == 3);
	CHECK(found.Lines == std::vector<std::size_t>{0, 2});
}

TEST_CASE("A word written in letters beyond ASCII is found whole as well")
{
	const std::string text = "zażółć + zażółćmy + zażółć";
	const Core::TextBuffer buffer(text);

	const Graphite::SelectionOccurrences found = Graphite::FindSelectionOccurrences(buffer, Covering(text, "zażółć"));

	CHECK(StartsOf(found) == std::vector<std::size_t>{0, text.rfind("zażółć")});
}

TEST_CASE("A selection inside a word finds nothing")
{
	const std::string text = "_steam = _steam;";
	const Core::TextBuffer buffer(text);

	CHECK(Graphite::FindSelectionOccurrences(buffer, Covering(text, "steam")).Matches.empty());
}

TEST_CASE("A selection across two words finds nothing")
{
	const std::string text = "a b a b";
	const Core::TextBuffer buffer(text);

	CHECK(Graphite::FindSelectionOccurrences(buffer, Covering(text, "a b")).Matches.empty());
}

TEST_CASE("A selection of punctuation finds nothing")
{
	const std::string text = "a + b + c";
	const Core::TextBuffer buffer(text);

	CHECK(Graphite::FindSelectionOccurrences(buffer, Covering(text, "+")).Matches.empty());
}

TEST_CASE("A caret with nothing selected finds nothing")
{
	const Core::TextBuffer buffer("a a a");

	CHECK(Graphite::FindSelectionOccurrences(buffer, Core::OffsetRange{.From = 2, .To = 2}).Matches.empty());
}

TEST_CASE("The occurrences shown follow an edit that adds another")
{
	const std::string text = "a = a;";
	Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;
	const Core::OpenItem item = ItemSelecting(Covering(text, "a"));

	REQUIRE(Graphite::OccurrencesToShow(memo, buffer, item, kEnabled).Matches.size() == 2);

	buffer.Insert(buffer.GetLength(), " a");

	CHECK(Graphite::OccurrencesToShow(memo, buffer, item, kEnabled).Matches.size() == 3);
}

TEST_CASE("The occurrences shown follow the selection onto another word")
{
	const std::string text = "a b a b b";
	const Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;

	REQUIRE(Graphite::OccurrencesToShow(memo, buffer, ItemSelecting(Covering(text, "a")), kEnabled).Matches.size() == 2);

	CHECK(Graphite::OccurrencesToShow(memo, buffer, ItemSelecting(Covering(text, "b")), kEnabled).Matches.size() == 3);
}

TEST_CASE("A word found only where it is selected shows nothing, not even on the scrollbar")
{
	const std::string text = "once = twice + twice;";
	const Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;

	const Graphite::SelectionOccurrences& shown =
		Graphite::OccurrencesToShow(memo, buffer, ItemSelecting(Covering(text, "once")), kEnabled);

	CHECK(shown.Matches.empty());
	CHECK(shown.Lines.empty());
}

TEST_CASE("No occurrences are shown while the setting is off")
{
	const std::string text = "a = a;";
	const Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;

	CHECK(Graphite::OccurrencesToShow(memo, buffer, ItemSelecting(Covering(text, "a")), kDisabled).Matches.empty());
}

TEST_CASE("No occurrences are shown while the find bar shows matches of its own")
{
	const std::string text = "a = a;";
	const Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;
	Core::OpenItem item = ItemSelecting(Covering(text, "a"));

	item.Find.Open = true;
	item.Find.Matches = {Core::FindMatch{.From = 0, .To = 1}};

	CHECK(Graphite::OccurrencesToShow(memo, buffer, item, kEnabled).Matches.empty());
}

TEST_CASE("Occurrences are shown while the find bar is open with nothing found")
{
	const std::string text = "a = a;";
	const Core::TextBuffer buffer(text);
	Graphite::OccurrenceMemo memo;
	Core::OpenItem item = ItemSelecting(Covering(text, "a"));

	item.Find.Open = true;

	CHECK(Graphite::OccurrencesToShow(memo, buffer, item, kEnabled).Matches.size() == 2);
}

TEST_CASE("Each line holding an occurrence is marked where it sits on the track, no shorter than the least height")
{
	constexpr float kLeastHeight = 0.02f;

	const std::vector<Graphite::OccurrenceMark> marks =
		Graphite::BuildOccurrenceOverview({0, 50}, LineTopOfAHundred, kLeastHeight);

	REQUIRE(marks.size() == 2);
	CHECK(marks[0].From == doctest::Approx(0.0f));
	CHECK(marks[0].To == doctest::Approx(0.02f));
	CHECK(marks[1].From == doctest::Approx(0.5f));
	CHECK(marks[1].To == doctest::Approx(0.52f));
}

TEST_CASE("Marks for lines that touch on the track become one")
{
	constexpr float kLeastHeight = 0.005f;

	const std::vector<Graphite::OccurrenceMark> marks =
		Graphite::BuildOccurrenceOverview({10, 11, 40}, LineTopOfAHundred, kLeastHeight);

	REQUIRE(marks.size() == 2);
	CHECK(marks[0].From == doctest::Approx(0.10f));
	CHECK(marks[0].To == doctest::Approx(0.12f));
	CHECK(marks[1].From == doctest::Approx(0.40f));
}

TEST_CASE("A run of many marked lines is placed by asking where a few of them sit, not every one")
{
	constexpr std::size_t kLineCount = 100000;
	constexpr float kLeastHeight = 0.002f;
	constexpr std::size_t kLinearAsks = kLineCount * 2;
	constexpr std::size_t kAtMost = kLinearAsks / 10;

	std::vector<std::size_t> lines(kLineCount);
	std::iota(lines.begin(), lines.end(), std::size_t{0});

	std::size_t asked = 0;
	const auto lineTop = [&asked](std::size_t line) {
		++asked;
		return static_cast<float>(line) / static_cast<float>(kLineCount);
	};

	const std::vector<Graphite::OccurrenceMark> marks = Graphite::BuildOccurrenceOverview(lines, lineTop, kLeastHeight);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.0f));
	CHECK(marks[0].To == doctest::Approx(1.0f));
	CHECK(asked < kAtMost);
}

TEST_CASE("A mark on the last line is pushed back inside the track rather than cut short")
{
	constexpr float kLeastHeight = 0.05f;

	const std::vector<Graphite::OccurrenceMark> marks =
		Graphite::BuildOccurrenceOverview({99}, LineTopOfAHundred, kLeastHeight);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.95f));
	CHECK(marks[0].To == doctest::Approx(1.0f));
}
