#include <doctest/doctest.h>

#include "Search/FolderSearch.h"
#include "Search/SearchService.h"

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	constexpr std::size_t kManyFilesCount = 20;

	void WriteFile(const std::filesystem::path& path, std::string_view contents)
	{
		std::filesystem::create_directories(path.parent_path());

		std::ofstream outputStream(path, std::ios::binary);
		outputStream << contents;
	}

	std::vector<Graphite::SearchHit> RunToEnd(Graphite::SearchService& service, DeferredJobs& jobs,
											  Graphite::SearchProgress& progress)
	{
		jobs.RunAll();

		std::vector<Graphite::SearchHit> hits;
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

	// A folder searched where it lives: answers with what it was set up with, and remembers what it was asked.
	struct SearchedFolder
	{
		Core::Result<Graphite::FolderSearchFound> Answer = Graphite::FolderSearchFound{};
		std::vector<std::filesystem::path> Skipped;
		bool Searched = false;
		int Opened = 0;

		Graphite::FolderSearchOpener Opener()
		{
			return [this](const std::filesystem::path&) -> Core::Scope<Graphite::FolderSearch> {
				++Opened;
				return std::make_unique<Search>(*this);
			};
		}

		class Search final : public Graphite::FolderSearch
		{
		public:
			explicit Search(SearchedFolder& folder) : m_Folder(folder) {}

			Core::Result<std::vector<std::string>> ListFiles(bool, const Core::CancellationToken&) override { return std::vector<std::string>{}; }

			Core::Result<Graphite::FolderSearchFound> SearchText(const Graphite::SearchQuery&, bool, const std::vector<std::filesystem::path>& skip,
																 const Core::CancellationToken&) override
			{
				m_Folder.Searched = true;
				m_Folder.Skipped = skip;
				return m_Folder.Answer;
			}

		private:
			SearchedFolder& m_Folder;
		};
	};
}

TEST_CASE("SearchService finds a hit in a file on disk")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "int alpha = 1;\nint beta = 2;\n");

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 1);
	CHECK(hits[0].Text == "int beta = 2;");
	CHECK_FALSE(progress.Running);
	CHECK(progress.FilesScanned == 1);
}

TEST_CASE("SearchService reports a search as running until its job has run")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "needle\n");

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), "needle"));

	std::vector<Graphite::SearchHit> hits;
	Graphite::SearchProgress progress;
	service.Drain(hits, progress);

	CHECK(progress.Running);
	CHECK(hits.empty());
	CHECK(jobs.Count() == 1);
}

TEST_CASE("SearchService applies the file mask")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "needle\n");
	WriteFile(temp.GetPath() / "b.md", "needle\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Query.Mask = "*.cpp";

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

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

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

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

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(std::move(request));

	Graphite::SearchProgress progress;

	CHECK(RunToEnd(service, jobs, progress).empty());
}

TEST_CASE("SearchService skips a binary file and counts it")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "needle\n");
	WriteFile(temp.GetPath() / "b.bin", std::string("needle\0\0\0binary", 15));

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), "needle"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

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

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

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

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};

	service.Start(Request(temp.GetPath(), "alpha"));
	jobs.RunAll();

	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	for (const Graphite::SearchHit& hit : hits)
	{
		CHECK(hit.Text == "beta");
	}

	CHECK(hits.size() == kManyFilesCount);
}

TEST_CASE("SearchService runs nothing for an empty query instead of walking the tree")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "a.cpp", "alpha\n");
	WriteFile(temp.GetPath() / "b.cpp", "beta\n");

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), ""));

	CHECK(jobs.Count() == 0);

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

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

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), "alpha"));
	service.Cancel();

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	CHECK_FALSE(progress.Running);
	CHECK(progress.FilesScanned == 0);
	CHECK(hits.empty());
}

TEST_CASE("SearchService searches only the listed files under the projects-only scope")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "listed.cpp", "needle\n");
	WriteFile(temp.GetPath() / "unlisted.cpp", "needle\n");

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Options.ProjectsOnly = true;
	request.ProjectFiles.push_back(temp.GetPath() / "listed.cpp");

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].File.filename() == "listed.cpp");
}

TEST_CASE("SearchService places a hit in a file with a byte order mark where the opened document has it")
{
	const TempDirectory temp;

	WriteFile(temp.GetPath() / "marked.cpp", "\xEF\xBB\xBFint beta = 2;\r\nint gamma = 3;\r\n");

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.Start(Request(temp.GetPath(), "beta"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Line == 0);
	CHECK(hits[0].From == 4);
	CHECK(hits[0].Text == "int beta = 2;");
}

TEST_CASE("SearchService searches a folder searched where it lives through that folder's search, and the folder's open documents itself")
{
	const TempDirectory temp;
	const std::filesystem::path open = temp.GetPath() / "open.cpp";
	WriteFile(open, "on disk\n");
	const std::filesystem::path elsewhere = temp.GetPath().parent_path() / "elsewhere.cpp";

	SearchedFolder folder;
	Graphite::FolderSearchFound found;
	found.Hits.push_back(Graphite::SearchHit{.File = temp.GetPath() / "far.cpp", .Line = 3, .Text = "a needle", .From = 2, .To = 8});
	found.Scanned = 5;
	found.Skipped = 2;
	found.Unreadable = 1;
	folder.Answer = found;

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.SetFolderSearch(folder.Opener());

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Unsaved.push_back(Graphite::UnsavedFile{open, "the needle, unsaved\n"});
	request.Unsaved.push_back(Graphite::UnsavedFile{elsewhere, "a needle outside\n"});
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	REQUIRE(folder.Searched);
	CHECK(folder.Skipped.size() == 2);
	REQUIRE(hits.size() == 2);
	CHECK(hits[0].File.filename() == "open.cpp");
	CHECK(hits[0].Text == "the needle, unsaved");
	CHECK(hits[1].File.filename() == "far.cpp");
	CHECK(hits[1].Line == 3);
	CHECK(progress.FilesScanned == 6);
	CHECK(progress.FilesSkipped == 2);
	CHECK(progress.Unreadable == 1);
	CHECK_FALSE(progress.Running);
}

TEST_CASE("SearchService ends a folder search that failed with nothing found")
{
	const TempDirectory temp;

	SearchedFolder folder;
	folder.Answer = Core::Error{Core::ErrorCode::IoFailure, "the WSL agent stopped"};

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.SetFolderSearch(folder.Opener());
	service.Start(Request(temp.GetPath(), "needle"));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	CHECK(folder.Searched);
	CHECK(hits.empty());
	CHECK_FALSE(progress.Running);
}

TEST_CASE("SearchService reads the projects' files itself even in a folder searched where it lives")
{
	const TempDirectory temp;
	WriteFile(temp.GetPath() / "listed.cpp", "needle\n");

	SearchedFolder folder;

	DeferredJobs jobs;
	Graphite::SearchService service{jobs.Submitter()};
	service.SetFolderSearch(folder.Opener());

	Graphite::SearchStart request = Request(temp.GetPath(), "needle");
	request.Options.ProjectsOnly = true;
	request.ProjectFiles.push_back(temp.GetPath() / "listed.cpp");
	service.Start(std::move(request));

	Graphite::SearchProgress progress;
	const std::vector<Graphite::SearchHit> hits = RunToEnd(service, jobs, progress);

	CHECK(folder.Opened == 0);
	REQUIRE(hits.size() == 1);
	CHECK(hits[0].File.filename() == "listed.cpp");
}
