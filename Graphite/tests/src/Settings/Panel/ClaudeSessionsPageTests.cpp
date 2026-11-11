#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "FakeHttpClient.h"
#include "SettingsPageFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Sessions/ClaudeSettings.h"
#include "Sessions/PriceUpdates.h"
#include "Sessions/PricingPage.h"
#include "Settings/Panel/ClaudeSessionsPage.h"

#include "Core/FileSystem/FileSystem.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr float kWideWindowWidth = 900.0f;
	constexpr float kNarrowWindowWidth = 320.0f;

	struct ClaudeScreen
	{
		TempDirectory Temp;
		TempAppSettings Settings;
		Graphite::ClaudeSettings Claude{Settings.Get()};
		Core::Ref<FakeHttpClient> Http = Core::CreateRef<FakeHttpClient>();
		DeferredJobs Jobs;
		Graphite::PriceUpdates Prices{Claude, Http, Jobs.Submitter(), [] { return std::string{"2026-11-11"}; }};
		Graphite::ClaudeSessionsPage Page{Claude, Prices};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("The Claude page holds the program and the prices")
{
	ClaudeScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Program", "Prices"});
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"Claude program", "Price list", "Prices"});
}

TEST_CASE("The price list says the built-in prices are in use until the pricing page is fetched")
{
	ClaudeScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Price list");

	REQUIRE(row.has_value());
	CHECK(row->Description.find("not been fetched") != std::string::npos);
	CHECK(row->Problem.empty());
}

TEST_CASE("The price list draws its Refresh button")
{
	ClaudeScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Price list");

	REQUIRE(row.has_value());
	CHECK(static_cast<bool>(row->DrawControl));
	CHECK(row->ControlWidth > 0.0f);
}

TEST_CASE("The price list says the day the pricing page was fetched, and fetching while it runs")
{
	ClaudeScreen screen;
	screen.Claude.AdoptFetchedPrices({}, "2026-11-10");

	std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Price list");
	REQUIRE(row.has_value());
	CHECK(row->Description.find("fetched on 2026-11-10") != std::string::npos);

	screen.Prices.Fetch();

	row = screen.Frames.FindRow("Price list");
	REQUIRE(row.has_value());
	CHECK(row->Description.find("Fetching") != std::string::npos);
}

TEST_CASE("The price list says why the last fetch failed")
{
	ClaudeScreen screen;
	screen.Http->Answer(std::string{Graphite::kPricingPageUrl}, Core::kHttpStatusOk, "# Pricing\n\nMoved.\n");

	screen.Prices.Fetch();
	screen.Jobs.RunAll();
	screen.Prices.Poll();

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Price list");

	REQUIRE(row.has_value());
	CHECK(row->Problem.find("The last fetch failed") != std::string::npos);
}

TEST_CASE("The program row names the file that runs")
{
	ClaudeScreen screen;
	const std::filesystem::path program = screen.Temp.GetPath() / "claude.exe";
	std::ofstream{program} << "stand-in";
	screen.Claude.SetBinary(program);

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Claude program");

	REQUIRE(row.has_value());
	CHECK(row->Description == "Runs " + Core::ToUtf8(program) + ".");
	CHECK(row->Problem.empty());
}

TEST_CASE("The Claude page draws wide and narrow without an ImGui assertion")
{
	ClaudeScreen screen;

	screen.Frames.DrawPageTwice(kWideWindowWidth);
	screen.Frames.DrawPageTwice(kNarrowWindowWidth);

	CHECK(screen.Frames.CardHeadings().size() == 2);
}

TEST_CASE("The Claude page draws with an edited price, a fetched one and a failed fetch without an ImGui assertion")
{
	ClaudeScreen screen;
	screen.Claude.AdoptFetchedPrices({{"claude-opus-4-5", Graphite::StandardPrice(5.0, 25.0, 0.5)}}, "2026-11-10");
	screen.Claude.SetPrice("claude-opus-5", Graphite::StandardPrice(1.0, 2.0, 0.1));
	screen.Prices.Fetch();
	screen.Jobs.RunAll();
	screen.Prices.Poll();

	screen.Frames.DrawPageTwice(kWideWindowWidth);
	screen.Frames.DrawPageTwice(kNarrowWindowWidth);

	CHECK(screen.Frames.CardHeadings().size() == 2);
}
