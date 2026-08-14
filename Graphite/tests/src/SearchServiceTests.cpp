#include <doctest/doctest.h>

#include "Search/SearchService.h"
#include "TempDirectory.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace {

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::filesystem::create_directories(path.parent_path());

		std::ofstream out(path, std::ios::binary);
		out << contents;
	}

	// Drains until the service stops running or the deadline passes. Returns everything it saw.
	// Fails by timing out rather than hanging the suite.
	std::vector<Graphite::SearchHit> RunToEnd(Graphite::SearchService& service,
											  Graphite::SearchProgress& progress)
	{
		std::vector<Graphite::SearchHit> hits;

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (std::chrono::steady_clock::now() < deadline)
		{
			service.Drain(hits, progress);

			if (!progress.Running)
			{
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		// One more, to collect anything produced between the last drain and the stop.
		service.Drain(hits, progress);

		return hits;
	}

	Graphite::SearchStart Request(const std::filesystem::path& root, std::string needle)
	{
		Graphite::SearchStart request;
		request.Root = root;
		request.Query.Needle = std::move(needle);
		request.Options.RespectGitIgnore = false;

		return request;
	}
}

TEST_CASE("SearchService finds a hit in a file on disk")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "int alpha = 1;\nint beta = 2;\n");

	Graphite::SearchService service;
	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "int beta = 2;");
	CHECK_FALSE(progress.Running);
	CHECK(progress.FilesScanned == 1);
}

TEST_CASE("SearchService applies the file mask")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "needle\n");
	WriteFile(temp.GetPath() / "b.md", "needle\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Query.Mask = "*.cpp";

	Graphite::SearchService service;
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].File.filename() == "a.cpp");
}

// The reason unsaved text is copied at all: what is on screen wins over what is on disk.
TEST_CASE("SearchService searches an open document's text rather than the file's")
{
	const TempDirectory temp;

	const std::filesystem::path file = temp.GetPath() / "a.cpp";

	WriteFile(file, "old text\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "fresh");
	request.Unsaved.push_back(Graphite::UnsavedFile{file, "fresh text\n"});

	Graphite::SearchService service;
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Text == "fresh text");
	CHECK(progress.FilesScanned == 1);
}

TEST_CASE("SearchService does not find what only the saved file still says")
{
	const TempDirectory temp;

	const std::filesystem::path file = temp.GetPath() / "a.cpp";

	WriteFile(file, "removed\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "removed");
	request.Unsaved.push_back(Graphite::UnsavedFile{file, "gone now\n"});

	Graphite::SearchService service;
	service.Start(std::move(request));

	Graphite::SearchProgress progress;

	CHECK(RunToEnd(service, progress).empty());
}

TEST_CASE("SearchService skips a binary file and counts it")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "needle\n");
	WriteFile(temp.GetPath() / "b.bin", std::string("needle\0\0\0binary", 15));

	Graphite::SearchService service;
	service.Start(Request(temp.GetPath(), "needle"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].File.filename() == "a.cpp");
	CHECK(progress.FilesSkipped == 1);
}

// Distinguishes Unreadable from Skipped: a probe failure is neither binary nor too large, so an
// implementation that folded every non-scan outcome into FilesSkipped would still pass every
// other test in this file.
TEST_CASE("SearchService counts a file it cannot probe as unreadable")
{
	const TempDirectory temp;

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Options.ProjectsOnly = true;
	request.ProjectFiles.push_back(temp.GetPath() / "missing.cpp");

	Graphite::SearchService service;
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	CHECK(hits.empty());
	CHECK(progress.Unreadable == 1);
	CHECK(progress.FilesSkipped == 0);
}

// What this can actually pin is deterministic: Start clears the shared hit vector under the
// same lock it bumps the generation in, so nothing the first search already contributed
// before the second Start survives into the second search's results. Whether the first search
// contributes anything at all before it is superseded is opportunistic even with the wait
// below — its worker only polls the cancellation token once per file, so it can still stop
// before a single contribute call. This test does not exercise the generation guard inside
// SearchService::Start's contribute lambda: that guard covers a narrower window (a
// contribution already inside the lock when the next Start's clear-and-bump runs) that no
// sleep can reliably open. See the comment on the guard itself.
TEST_CASE("SearchService shows only the newest search's results")
{
	const TempDirectory temp;

	for (int file = 0; file < 200; ++file)
	{
		WriteFile(temp.GetPath() / ("f" + std::to_string(file) + ".cpp"), "alpha\nbeta\n");
	}

	Graphite::SearchService service;

	service.Start(Request(temp.GetPath(), "alpha"));

	// Wait for the first search to have genuinely scanned something, so superseding it
	// exercises a search that ran rather than one that may never have started.
	Graphite::SearchProgress firstProgress;
	const auto firstScanDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	while (firstProgress.FilesScanned == 0 &&
		   std::chrono::steady_clock::now() < firstScanDeadline)
	{
		std::vector<Graphite::SearchHit> discarded;
		service.Drain(discarded, firstProgress);

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	REQUIRE(firstProgress.FilesScanned > 0);

	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	// Every hit belongs to the second search.
	for (const Graphite::SearchHit& hit : hits)
	{
		CHECK(hit.Text == "beta");
	}

	CHECK_FALSE(hits.empty());
}

TEST_CASE("SearchService stops when cancelled")
{
	const TempDirectory temp;

	for (int file = 0; file < 200; ++file)
	{
		WriteFile(temp.GetPath() / ("f" + std::to_string(file) + ".cpp"), "alpha\n");
	}

	Graphite::SearchService service;
	service.Start(Request(temp.GetPath(), "alpha"));
	service.Cancel();

	Graphite::SearchProgress progress;
	RunToEnd(service, progress);

	CHECK_FALSE(progress.Running);
	CHECK(progress.FilesScanned < 200);

	// Pins the stop, not just the flag: a service that only flipped Running without actually
	// halting the worker would keep climbing here. A bounded wait, not an open-ended one — this
	// is well past the 1ms drain interval RunToEnd already used to settle.
	const std::size_t scannedAfterStop = progress.FilesScanned;

	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	std::vector<Graphite::SearchHit> hits;
	service.Drain(hits, progress);

	CHECK(progress.FilesScanned == scannedAfterStop);
}

TEST_CASE("SearchService searches only the listed files under the projects-only scope")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "listed.cpp", "needle\n");
	WriteFile(temp.GetPath() / "unlisted.cpp", "needle\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Options.ProjectsOnly = true;
	request.ProjectFiles.push_back(temp.GetPath() / "listed.cpp");

	Graphite::SearchService service;
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].File.filename() == "listed.cpp");
}
