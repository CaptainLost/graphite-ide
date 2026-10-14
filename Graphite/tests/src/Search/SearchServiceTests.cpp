#include <doctest/doctest.h>

#include "Search/SearchService.h"
#include "TempDirectory.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string_view>
#include <thread>

namespace {

	constexpr std::chrono::seconds kSearchDeadline{10};
	constexpr std::chrono::milliseconds kPollInterval{1};
	constexpr std::chrono::milliseconds kSettleInterval{200};
	constexpr std::size_t kManyFilesCount = 200;

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::filesystem::create_directories(path.parent_path());

		std::ofstream outputStream(path, std::ios::binary);
		outputStream << contents;
	}

	std::vector<Graphite::SearchHit> RunToEnd(Graphite::SearchService& service,
											  Graphite::SearchProgress& progress)
	{
		std::vector<Graphite::SearchHit> hits;

		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + kSearchDeadline;

		while (std::chrono::steady_clock::now() < deadline)
		{
			service.Drain(hits, progress);

			if (!progress.Running)
			{
				break;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

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

TEST_CASE("SearchService searches an open document's text rather than the file's on disk")
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

TEST_CASE("SearchService counts a file it cannot probe as unreadable and not as skipped")
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

TEST_CASE("SearchService shows only the newest search's results")
{
	const TempDirectory temp;

	for (std::size_t fileIndex = 0; fileIndex < kManyFilesCount; ++fileIndex)
	{
		WriteFile(temp.GetPath() / ("f" + std::to_string(fileIndex) + ".cpp"), "alpha\nbeta\n");
	}

	Graphite::SearchService service;

	service.Start(Request(temp.GetPath(), "alpha"));

	Graphite::SearchProgress firstProgress;
	const std::chrono::steady_clock::time_point firstScanDeadline =
		std::chrono::steady_clock::now() + kSearchDeadline;

	while (firstProgress.FilesScanned == 0 &&
		   std::chrono::steady_clock::now() < firstScanDeadline)
	{
		std::vector<Graphite::SearchHit> discarded;
		service.Drain(discarded, firstProgress);

		std::this_thread::sleep_for(kPollInterval);
	}

	REQUIRE(firstProgress.FilesScanned > 0);

	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	for (const Graphite::SearchHit& hit : hits)
	{
		CHECK(hit.Text == "beta");
	}

	CHECK_FALSE(hits.empty());
}

TEST_CASE("SearchService runs nothing for an empty query instead of walking the tree")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "alpha\n");
	WriteFile(temp.GetPath() / "b.cpp", "beta\n");

	Graphite::SearchService service;
	service.Start(Request(temp.GetPath(), ""));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, progress);

	CHECK(hits.empty());
	CHECK(progress.FilesScanned == 0);
	CHECK(progress.FilesSkipped == 0);
	CHECK(progress.Unreadable == 0);
	CHECK_FALSE(progress.Running);
}

TEST_CASE("SearchService stops when cancelled and its worker stops scanning")
{
	const TempDirectory temp;

	for (std::size_t fileIndex = 0; fileIndex < kManyFilesCount; ++fileIndex)
	{
		WriteFile(temp.GetPath() / ("f" + std::to_string(fileIndex) + ".cpp"), "alpha\n");
	}

	Graphite::SearchService service;
	service.Start(Request(temp.GetPath(), "alpha"));
	service.Cancel();

	Graphite::SearchProgress progress;
	RunToEnd(service, progress);

	CHECK_FALSE(progress.Running);
	CHECK(progress.FilesScanned < kManyFilesCount);

	const std::size_t scannedAfterStop = progress.FilesScanned;

	std::this_thread::sleep_for(kSettleInterval);

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
