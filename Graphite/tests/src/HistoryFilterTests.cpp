#include <doctest/doctest.h>

#include "Git/HistoryFilter.h"

#include <string>

using Graphite::BuildLogRequest;
using Graphite::HistoryFilter;
using Graphite::MatchesRefFilter;

TEST_CASE("A ref filter with no text accepts every branch")
{
	CHECK(MatchesRefFilter("refs/heads/main", ""));
	CHECK(MatchesRefFilter("refs/heads/main", "   "));
}

TEST_CASE("A ref is matched by any part of its name")
{
	CHECK(MatchesRefFilter("refs/remotes/origin/feature/encoding", "encoding"));
	CHECK(MatchesRefFilter("refs/remotes/origin/feature/encoding", "origin"));
}

TEST_CASE("A ref filter ignores case and surrounding whitespace")
{
	CHECK(MatchesRefFilter("refs/heads/Encoding", " encoding "));
}

TEST_CASE("A ref the text does not name is not matched")
{
	CHECK_FALSE(MatchesRefFilter("refs/heads/main", "encoding"));
}

TEST_CASE("A log request carries the selected ref")
{
	const Core::LogRequest request =
		BuildLogRequest(std::string{"refs/heads/main"}, HistoryFilter{}, std::nullopt);

	REQUIRE(request.Ref.has_value());
	CHECK(*request.Ref == "refs/heads/main");
}

TEST_CASE("A log request with no ref selected asks about HEAD")
{
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, std::nullopt);

	CHECK_FALSE(request.Ref.has_value());
}

TEST_CASE("A log request carries the typed filter text")
{
	HistoryFilter filter;
	filter.Text = "encoding";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, std::nullopt);

	CHECK(request.Filter.Text == "encoding");
}

TEST_CASE("A log request carries the typed path")
{
	HistoryFilter filter;
	filter.Path = "Core/src/Core/Git/Repository.cpp";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, std::nullopt);

	REQUIRE(request.File.has_value());
	CHECK(*request.File == std::filesystem::path{"Core/src/Core/Git/Repository.cpp"});
}

TEST_CASE("A path box holding nothing but whitespace narrows nothing")
{
	// Narrowing by file diffs every commit the walk passes, so an empty box that still set
	// File would turn the ordinary branch log into the most expensive query there is.
	HistoryFilter filter;
	filter.Path = "   ";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, std::nullopt);

	CHECK_FALSE(request.File.has_value());
}

TEST_CASE("A log request without a page size keeps the default one")
{
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, std::nullopt);

	CHECK(request.MaxCount == Core::LogRequest{}.MaxCount);
}

TEST_CASE("A log request asked for a bigger page carries that size")
{
	// What load-more asks for: the model holds one snapshot per query, so a longer page is
	// requested rather than a second one appended.
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, 40);

	CHECK(request.MaxCount == 40);
}

using Graphite::SettlingFilter;

namespace {

	// Past kSettleSeconds in one step: every test below cares about whether a change fires,
	// not about how many frames it took to get there.
	constexpr float kSettled = SettlingFilter::kSettleSeconds + 0.01f;

	// The frame a change is typed in. Its step never counts toward the wait, however long it
	// is — after the loop has slept, it is the whole sleep.
	void KeystrokeFrame(SettlingFilter& settling)
	{
		REQUIRE_FALSE(settling.Advance(Core::Timestep{kSettled}));
	}

	HistoryFilter TextOf(std::string text)
	{
		HistoryFilter filter;
		filter.Text = std::move(text);
		return filter;
	}
}

TEST_CASE("A filter nobody typed into never asks for anything")
{
	SettlingFilter settling;

	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A change does not ask before it has settled")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

	// Querying on the keystroke would put a query behind every letter of a word, each one
	// walking history the next one throws away.
	CHECK_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds / 2.0f}));
}

TEST_CASE("A change asks once it has settled")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);

	CHECK(settling.Advance(Core::Timestep{kSettled}));
	CHECK(settling.Get().Text == "enc");
}

TEST_CASE("A settled change asks exactly once")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);

	REQUIRE(settling.Advance(Core::Timestep{kSettled}));
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("Typing again before the wait is over starts the wait over")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);

	REQUIRE_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds * 0.75f}));

	settling.Set(TextOf("enco"));
	KeystrokeFrame(settling);

	CHECK_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds * 0.75f}));
	CHECK(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds * 0.75f}));
	CHECK(settling.Get().Text == "enco");
}

TEST_CASE("Text erased back to what was already asked for asks nothing")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);
	REQUIRE(settling.Advance(Core::Timestep{kSettled}));

	settling.Set(TextOf("encoding"));
	settling.Set(TextOf("enc"));

	// The page on screen already answers this text, so re-querying would replace it with
	// an identical one and lose the reader's scroll position for nothing.
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A change to the path alone settles the same way")
{
	SettlingFilter settling;

	HistoryFilter filter;
	filter.Path = "Core/src/Core/Git/Repository.cpp";
	settling.Set(filter);
	KeystrokeFrame(settling);

	CHECK(settling.Advance(Core::Timestep{kSettled}));
	CHECK(settling.Get().Path == "Core/src/Core/Git/Repository.cpp");
}

TEST_CASE("A reset filter is empty and has nothing pending")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

	settling.Reset();

	// A workspace that opened after another one left asks for its first page unfiltered;
	// a change left pending from the previous repository would fire over the top of it.
	CHECK(settling.Get().Text.empty());
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A filter somebody else already asked for does not ask again")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

	// What picking a branch mid-typing does: it queries with the text as it stands now,
	// filter and all. Without being told, the wait would run out a moment later and send
	// the same query a second time — two walks of the history for one gesture.
	settling.Accept();

	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A filter changed after being asked for still settles")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	settling.Accept();

	settling.Set(TextOf("encoding"));
	KeystrokeFrame(settling);

	CHECK(settling.Advance(Core::Timestep{kSettled}));
	CHECK(settling.Get().Text == "encoding");
}

TEST_CASE("The frame a change is typed in does not count toward its wait")
{
	SettlingFilter settling;
	settling.Set(TextOf("e"));

	// The frame loop sleeps when idle, so the keystroke that wakes it arrives on a frame
	// whose step is the whole sleep. Counting it would query the history for one letter.
	CHECK_FALSE(settling.Advance(Core::Timestep{10.0f}));
	CHECK_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds / 2.0f}));
	CHECK(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds}));
}
