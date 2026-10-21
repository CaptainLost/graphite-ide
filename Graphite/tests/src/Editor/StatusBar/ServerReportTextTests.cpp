#include <doctest/doctest.h>

#include "Editor/StatusBar/ServerReportText.h"

#include <chrono>
#include <string>

namespace {

	constexpr std::chrono::minutes kTwoHoursEast{120};
	constexpr std::chrono::minutes kUtc{0};

	const std::chrono::system_clock::time_point kStartedAt =
		std::chrono::sys_days{std::chrono::year{2026} / 10 / 21} + std::chrono::hours{16} + std::chrono::minutes{1} +
		std::chrono::seconds{3};

	Graphite::ServerReport IndexingReport()
	{
		Graphite::ServerReport report;
		report.Status.State = Graphite::ServerState::Indexing;
		report.Status.Name = "clangd";
		report.Status.Activity = "Background index: 120/450";
		report.Root = "D:/Repos/IDE";
		report.OpenDocuments = 3;
		report.RequestsInFlight = 1;
		report.InStateFor = std::chrono::seconds{130};

		return report;
	}

	bool Says(const std::string& text, const std::string& words)
	{
		return text.find(words) != std::string::npos;
	}
}

TEST_CASE("A server's report says what it works on, where it runs and how busy it is")
{
	const std::string text = Graphite::ServerReportText(IndexingReport(), kUtc);

	CHECK(Says(text, "Working on: Background index: 120/450"));
	CHECK(Says(text, "Folder: D:/Repos/IDE"));
	CHECK(Says(text, "3 open files"));
	CHECK(Says(text, "1 request in flight"));
	CHECK(Says(text, "Indexing for 2 min 10 s"));
}

TEST_CASE("A server's report lists its recent events oldest first at the local time of day")
{
	Graphite::ServerReport report = IndexingReport();
	report.History = {Graphite::ServerEvent{.At = kStartedAt, .Text = "starting"},
					  Graphite::ServerEvent{.At = kStartedAt + std::chrono::seconds{1}, .Text = "ready"},
					  Graphite::ServerEvent{.At = kStartedAt + std::chrono::seconds{157},
											.Text = "finished indexing",
											.Took = std::chrono::seconds{155}}};

	const std::string text = Graphite::ServerReportText(report, kTwoHoursEast);

	CHECK(Says(text, "Recent:"));
	CHECK(Says(text, "18:01:03  starting"));
	CHECK(Says(text, "18:01:04  ready"));
	CHECK(Says(text, "18:03:40  finished indexing in 2 min 35 s"));
	CHECK(text.find("starting") < text.find("ready"));
}

TEST_CASE("A server's report keeps to its last eight events")
{
	Graphite::ServerReport report = IndexingReport();

	for (int eventIndex = 0; eventIndex < 12; ++eventIndex)
	{
		report.History.push_back(Graphite::ServerEvent{.At = kStartedAt + std::chrono::seconds{eventIndex},
													   .Text = "event " + std::to_string(eventIndex)});
	}

	const std::string text = Graphite::ServerReportText(report, kUtc);

	CHECK(Says(text, "event 4"));
	CHECK(Says(text, "event 11"));
	CHECK_FALSE(Says(text, "event 3"));
}

TEST_CASE("A server with no work and no events reported says only where it runs and how busy it is")
{
	Graphite::ServerReport report = IndexingReport();
	report.Status.Activity.clear();
	report.InStateFor.reset();

	const std::string text = Graphite::ServerReportText(report, kUtc);

	CHECK_FALSE(Says(text, "Working on"));
	CHECK_FALSE(Says(text, "Recent"));
	CHECK_FALSE(Says(text, " for "));
	CHECK(Says(text, "Folder: D:/Repos/IDE"));
}

TEST_CASE("A long stretch in one state is said in hours and minutes")
{
	Graphite::ServerReport report = IndexingReport();
	report.Status.State = Graphite::ServerState::Ready;
	report.InStateFor = std::chrono::seconds{3900};

	CHECK(Says(Graphite::ServerReportText(report, kUtc), "Ready for 1 h 5 min"));
}

TEST_CASE("A server that stopped mid-index is not said to be working on its index")
{
	Graphite::ServerReport report = IndexingReport();
	report.Status.State = Graphite::ServerState::Failed;

	CHECK_FALSE(Says(Graphite::ServerReportText(report, kUtc), "Working on"));
}
