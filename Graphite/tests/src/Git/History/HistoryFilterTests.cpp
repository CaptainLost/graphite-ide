#include <doctest/doctest.h>

#include "Git/History/HistoryFilter.h"

#include <string>

using Graphite::BuildLogRequest;
using Graphite::HistoryFilter;

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

TEST_CASE("A path box holding nothing but whitespace leaves the log unnarrowed by file")
{
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

TEST_CASE("A log request asked for a bigger page, as load-more does, carries that size")
{
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, 40);

	CHECK(request.MaxCount == 40);
}

using Graphite::SettlingFilter;

namespace {
	constexpr float kSettled = SettlingFilter::kSettleSeconds + 0.01f;

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

TEST_CASE("Setting the filter to what it already holds does not restart the wait")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);

	REQUIRE_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds * 0.75f}));

	settling.Set(TextOf("enc"));

	CHECK(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds * 0.75f}));
}

TEST_CASE("A filter nobody typed into never asks for anything")
{
	SettlingFilter settling;

	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A change does not ask on the keystroke, only once it has settled")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

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

TEST_CASE("Text erased back to what was already asked for asks nothing, keeping the page and its scroll position")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));
	KeystrokeFrame(settling);
	REQUIRE(settling.Advance(Core::Timestep{kSettled}));

	settling.Set(TextOf("encoding"));
	settling.Set(TextOf("enc"));

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

TEST_CASE("A reset filter is empty and leaves nothing pending to fire over the next repository")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

	settling.Reset();

	CHECK(settling.Get().Text.empty());
	CHECK_FALSE(settling.Advance(Core::Timestep{kSettled}));
}

TEST_CASE("A filter queried by someone else, such as a branch pick, does not query a second time")
{
	SettlingFilter settling;
	settling.Set(TextOf("enc"));

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

TEST_CASE("The frame a change is typed in does not count toward its wait, even when it carries the whole idle sleep")
{
	SettlingFilter settling;
	settling.Set(TextOf("e"));

	CHECK_FALSE(settling.Advance(Core::Timestep{10.0f}));
	CHECK_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds / 2.0f}));
	CHECK(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds}));
}
