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

// Staleness is detected rather than prevented, so a superseded search must contribute nothing
// even if it was still running when the next one began.
TEST_CASE("SearchService drops results from a superseded search")
{
	const TempDirectory temp;

	for (int file = 0; file < 200; ++file)
	{
		WriteFile(temp.GetPath() / ("f" + std::to_string(file) + ".cpp"), "alpha\nbeta\n");
	}

	Graphite::SearchService service;

	service.Start(Request(temp.GetPath(), "alpha"));
	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	// Every hit belongs to the second search. The first may have produced some before it was
	// cancelled; none of them may appear here.
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
