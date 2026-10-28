#include <doctest/doctest.h>

#include "Git/History/HistoryFilter.h"
#include "Git/History/HistoryReach.h"

#include <filesystem>
#include <string>

using Graphite::BuildLogRequest;
using Graphite::HistoryFilter;
using Graphite::HistoryReach;

TEST_CASE("A log request carries the selected ref")
{
	const Core::LogRequest request =
		BuildLogRequest(std::string{"refs/heads/main"}, HistoryFilter{}, HistoryReach{});

	REQUIRE(request.Ref.has_value());
	CHECK(*request.Ref == "refs/heads/main");
}

TEST_CASE("A log request with no ref selected asks about HEAD")
{
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, HistoryReach{});

	CHECK_FALSE(request.Ref.has_value());
}

TEST_CASE("A log request carries the typed filter text")
{
	HistoryFilter filter;
	filter.Text = "encoding";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, HistoryReach{});

	CHECK(request.Filter.Text == "encoding");
}

TEST_CASE("A log request carries the typed path")
{
	HistoryFilter filter;
	filter.Path = "Core/src/Core/Git/Repository.cpp";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, HistoryReach{});

	REQUIRE(request.File.has_value());
	CHECK(*request.File == std::filesystem::path{"Core/src/Core/Git/Repository.cpp"});
}

TEST_CASE("A path box holding nothing but whitespace leaves the log unnarrowed by file")
{
	HistoryFilter filter;
	filter.Path = "   ";

	const Core::LogRequest request = BuildLogRequest(std::nullopt, filter, HistoryReach{});

	CHECK_FALSE(request.File.has_value());
}

TEST_CASE("A log request with the default reach keeps the default page size and walk ceiling")
{
	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, HistoryReach{});

	CHECK(request.MaxCount == Core::LogRequest{}.MaxCount);
	CHECK(request.MaxWalk == Core::LogRequest{}.MaxWalk);
}

TEST_CASE("A log request carries the reach the reader asked for")
{
	const HistoryReach reach{.MaxCount = 1000, .MaxWalk = 40000};

	const Core::LogRequest request = BuildLogRequest(std::nullopt, HistoryFilter{}, reach);

	CHECK(request.MaxCount == 1000);
	CHECK(request.MaxWalk == 40000);
}

TEST_CASE("An empty request narrows nothing")
{
	CHECK_FALSE(Graphite::NarrowsHistory(Core::LogRequest{}));
}

TEST_CASE("Message text narrows the history")
{
	Core::LogRequest request;
	request.Filter.Text = "encoding";

	CHECK(Graphite::NarrowsHistory(request));
}

TEST_CASE("Message text of only spaces narrows nothing")
{
	Core::LogRequest request;
	request.Filter.Text = "   ";

	CHECK_FALSE(Graphite::NarrowsHistory(request));
}

TEST_CASE("A file narrows the history")
{
	Core::LogRequest request;
	request.File = std::filesystem::path{"Main.cpp"};

	CHECK(Graphite::NarrowsHistory(request));
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

TEST_CASE("A page of another ref or stash answers another history, a narrower or longer one of the same ref does not")
{
	Core::LogRequest head;

	Core::LogRequest branch;
	branch.Ref = "refs/heads/topic";

	Core::LogRequest stash;
	stash.Stash = Core::Oid{};

	Core::LogRequest fileOfHead = head;
	fileOfHead.File = std::filesystem::path("src/Main.cpp");
	fileOfHead.Filter.Text = "fix";
	fileOfHead.MaxCount = head.MaxCount + 500;

	CHECK(Graphite::AnswersAnotherHistory(branch, fileOfHead));
	CHECK(Graphite::AnswersAnotherHistory(head, branch));
	CHECK(Graphite::AnswersAnotherHistory(head, stash));
	CHECK_FALSE(Graphite::AnswersAnotherHistory(head, fileOfHead));
	CHECK_FALSE(Graphite::AnswersAnotherHistory(branch, branch));
}

TEST_CASE("The frame a change is typed in does not count toward its wait, even when it carries the whole idle sleep")
{
	SettlingFilter settling;
	settling.Set(TextOf("e"));

	CHECK_FALSE(settling.Advance(Core::Timestep{10.0f}));
	CHECK_FALSE(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds / 2.0f}));
	CHECK(settling.Advance(Core::Timestep{SettlingFilter::kSettleSeconds}));
}
