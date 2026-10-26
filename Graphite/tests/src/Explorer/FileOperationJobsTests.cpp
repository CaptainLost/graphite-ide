#include <doctest/doctest.h>

#include "Explorer/FileOperationJobs.h"

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Core/Base/Pointers.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

	using Clock = Graphite::FileOperationJobs::Clock;

	constexpr std::chrono::milliseconds kWithinTheLoaderWait{100};
	constexpr std::chrono::milliseconds kPastTheLoaderWait{250};

	const Clock::time_point kStart{};

	Graphite::RunningFileOperation Running(Graphite::FileOperationKind kind, const std::filesystem::path& target,
										   std::size_t copiedFiles)
	{
		return Graphite::RunningFileOperation{.Kind = kind,
											  .Target = target,
											  .StartedAt = kStart,
											  .CopiedFiles = Core::CreateRef<std::atomic<std::size_t>>(copiedFiles)};
	}

	class Gate
	{
	public:
		void Open()
		{
			const std::lock_guard<std::mutex> lock(m_Mutex);
			m_Open = true;
			m_Opened.notify_all();
		}

		void WaitUntilOpen()
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Opened.wait(lock, [this] { return m_Open; });
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Opened;
		bool m_Open = false;
	};

	constexpr std::size_t kFilesBeforeThePause = 2;

	struct CopyPausedAfterTwoFiles
	{
		Gate& Resume;

		void NoteThenPause(const Core::CopiedFileCount& noteCopied, std::size_t copiedFiles) const
		{
			noteCopied(copiedFiles);

			if (copiedFiles == kFilesBeforeThePause)
			{
				Resume.WaitUntilOpen();
			}
		}

		Core::Result<std::filesystem::path> operator()(const Graphite::RunningFileOperation& operation,
													   const Core::CopiedFileCount& noteCopied) const
		{
			const Core::CopiedFileCount noteThenPause = [this, &noteCopied](std::size_t copiedFiles) {
				NoteThenPause(noteCopied, copiedFiles);
			};

			return Graphite::RunFileOperation(operation, noteThenPause);
		}
	};

	Core::Result<std::filesystem::path> ThrowAsTheDiskGoes(const Graphite::RunningFileOperation&, const Core::CopiedFileCount&)
	{
		throw std::runtime_error("the disk went away");
	}

	std::optional<std::string> WaitForStatusText(const Graphite::FileOperationJobs& operations, const std::string& wanted)
	{
		constexpr std::chrono::seconds kTimeout{5};
		constexpr std::chrono::milliseconds kPollInterval{5};
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kTimeout;
		std::optional<std::string> text = operations.StatusText();

		while (text != wanted && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(kPollInterval);
			text = operations.StatusText();
		}

		return text;
	}
}

TEST_CASE("A duplicate says what it copies, and how many files so far once it has copied one")
{
	const std::filesystem::path target = std::filesystem::path{"C:/work"} / "Foo";

	CHECK(Graphite::RunningOperationText(Graphite::FileOperationKind::Duplicate, target, 0) ==
		  "Duplicating Foo\xE2\x80\xA6");
	CHECK(Graphite::RunningOperationText(Graphite::FileOperationKind::Duplicate, target, 1) ==
		  "Duplicating Foo\xE2\x80\xA6 1 file");
	CHECK(Graphite::RunningOperationText(Graphite::FileOperationKind::Duplicate, target, 1234) ==
		  "Duplicating Foo\xE2\x80\xA6 1,234 files");
}

TEST_CASE("A move to the Recycle Bin says what it moves and no count, which the shell cannot report")
{
	const std::filesystem::path target = std::filesystem::path{"C:/work"} / "Foo";

	CHECK(Graphite::RunningOperationText(Graphite::FileOperationKind::MoveToRecycleBin, target, 0) ==
		  "Moving Foo to the Recycle Bin\xE2\x80\xA6");
}

TEST_CASE("The status words are there from the start, for the status bar's loader to wait on with its spinner")
{
	const std::vector<Graphite::RunningFileOperation> running{
		Running(Graphite::FileOperationKind::Duplicate, "C:/work/Foo", 12)};

	const std::optional<std::string> text = Graphite::FileOperationsStatusText(running);

	REQUIRE(text.has_value());
	CHECK(*text == "Duplicating Foo\xE2\x80\xA6 12 files");
	CHECK_FALSE(Graphite::FileOperationsStatusText({}).has_value());
}

TEST_CASE("The status bar names the oldest operation and counts the others")
{
	const std::vector<Graphite::RunningFileOperation> running{
		Running(Graphite::FileOperationKind::MoveToRecycleBin, "C:/work/Old", 0),
		Running(Graphite::FileOperationKind::Duplicate, "C:/work/Middle", 0),
		Running(Graphite::FileOperationKind::Duplicate, "C:/work/New", 0)};

	const std::optional<std::string> text = Graphite::FileOperationsStatusText(running);

	REQUIRE(text.has_value());
	CHECK(*text == "Moving Old to the Recycle Bin\xE2\x80\xA6 and 2 more");
}

TEST_CASE("A path is busy from the moment its operation starts until its result is taken, and so is everything inside it")
{
	TempDirectory folder;
	const std::filesystem::path project = folder.MakeDirectory("project");
	folder.MakeFile("project/main.cpp", "int main() {}\n");
	folder.MakeFile("sibling.txt", "x");

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter()};

	operations.Start(Graphite::FileOperationKind::Duplicate, project, folder.GetPath(), kStart);

	CHECK(operations.IsBusy(project));
	CHECK(operations.IsBusy(project / "main.cpp"));
	CHECK_FALSE(operations.IsBusy(folder.GetPath() / "sibling.txt"));
	CHECK_FALSE(operations.IsBusy(folder.GetPath()));

	CHECK_FALSE(operations.ShowsBusy(project, kStart + kWithinTheLoaderWait));
	CHECK(operations.ShowsBusy(project / "main.cpp", kStart + kPastTheLoaderWait));

	REQUIRE(jobs.Count() == 1);
	CHECK(operations.TakeFinished().empty());
	CHECK(operations.IsBusy(project));

	jobs.RunAll();
	const std::vector<Graphite::FinishedFileOperation> finished = operations.TakeFinished();

	REQUIRE(finished.size() == 1);
	CHECK(finished[0].Kind == Graphite::FileOperationKind::Duplicate);
	CHECK(finished[0].Target == project);
	CHECK(finished[0].FolderOpenAtStart == folder.GetPath());
	REQUIRE(finished[0].CopyOrMovedPath.HasValue());
	CHECK(finished[0].CopyOrMovedPath.GetValue() == folder.GetPath() / "project copy");
	CHECK(std::filesystem::exists(folder.GetPath() / "project copy" / "main.cpp"));

	CHECK_FALSE(operations.IsBusy(project));
	CHECK_FALSE(operations.IsRunning());
}

TEST_CASE("A duplicate's copy is busy before the copy makes it, so its row shows the loader as soon as it appears")
{
	TempDirectory folder;
	const std::filesystem::path project = folder.MakeDirectory("project");
	folder.MakeFile("project/main.cpp", "int main() {}\n");
	const std::filesystem::path copy = folder.GetPath() / "project copy";

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter()};

	operations.Start(Graphite::FileOperationKind::Duplicate, project, folder.GetPath(), kStart);

	CHECK(operations.IsBusy(copy));
	CHECK(operations.IsBusy(copy / "main.cpp"));
	CHECK(operations.ShowsBusy(copy, kStart + kPastTheLoaderWait));

	jobs.RunAll();
	const std::vector<Graphite::FinishedFileOperation> finished = operations.TakeFinished();

	REQUIRE(finished.size() == 1);
	REQUIRE(finished[0].CopyOrMovedPath.HasValue());
	CHECK(finished[0].CopyOrMovedPath.GetValue() == copy);
	CHECK_FALSE(operations.IsBusy(copy));
}

TEST_CASE("A folder holding a busy path cannot be moved, but is no busy place to make something in")
{
	TempDirectory folder;
	const std::filesystem::path outer = folder.MakeDirectory("outer");
	const std::filesystem::path inner = folder.MakeDirectory("outer/inner");
	folder.MakeFile("outer/inner/file.txt", "x");
	folder.MakeFile("outer/free.txt", "x");

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter()};

	operations.Start(Graphite::FileOperationKind::MoveToRecycleBin, inner, folder.GetPath(), kStart);

	CHECK(operations.IsBusyOrHoldsABusyPath(outer));
	CHECK(operations.IsBusyOrHoldsABusyPath(folder.GetPath()));
	CHECK(operations.IsBusyOrHoldsABusyPath(inner / "file.txt"));
	CHECK_FALSE(operations.IsBusyOrHoldsABusyPath(outer / "free.txt"));
	CHECK_FALSE(operations.IsBusy(outer));
}

TEST_CASE("A duplicate's count is read while the copy runs, not only once it is over")
{
	TempDirectory folder;
	const std::filesystem::path project = folder.MakeDirectory("project");
	folder.MakeFile("project/a.txt", "a");
	folder.MakeFile("project/b.txt", "b");
	folder.MakeFile("project/c.txt", "c");
	folder.MakeFile("project/d.txt", "d");

	Gate resume;
	std::thread worker;
	const Core::JobSubmitter onItsOwnThread = [&worker](std::function<void()> job) { worker = std::thread{std::move(job)}; };
	Graphite::FileOperationJobs operations{onItsOwnThread, CopyPausedAfterTwoFiles{.Resume = resume}};

	operations.Start(Graphite::FileOperationKind::Duplicate, project, folder.GetPath(), kStart);

	const std::optional<std::string> midway = WaitForStatusText(operations, "Duplicating project\xE2\x80\xA6 2 files");
	const bool stillRunning = operations.TakeFinished().empty();

	resume.Open();
	worker.join();

	CHECK(stillRunning);
	REQUIRE(midway.has_value());
	CHECK(*midway == "Duplicating project\xE2\x80\xA6 2 files");

	const std::vector<Graphite::FinishedFileOperation> finished = operations.TakeFinished();
	REQUIRE(finished.size() == 1);
	CHECK(finished[0].CopyOrMovedPath.HasValue());
}

TEST_CASE("A move to the Recycle Bin that fails hands back the failure with the path it was asked to move")
{
	TempDirectory folder;
	const std::filesystem::path missing = folder.GetPath() / "gone.txt";

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter()};

	operations.Start(Graphite::FileOperationKind::MoveToRecycleBin, missing, folder.GetPath(), kStart);
	jobs.RunAll();

	const std::vector<Graphite::FinishedFileOperation> finished = operations.TakeFinished();

	REQUIRE(finished.size() == 1);
	CHECK(finished[0].Target == missing);
	REQUIRE_FALSE(finished[0].CopyOrMovedPath.HasValue());
	CHECK(finished[0].CopyOrMovedPath.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("An operation that throws hands back a failure naming why, and its path is free again")
{
	TempDirectory folder;
	const std::filesystem::path project = folder.MakeDirectory("project");

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter(), ThrowAsTheDiskGoes};

	operations.Start(Graphite::FileOperationKind::Duplicate, project, folder.GetPath(), kStart);
	jobs.RunAll();

	const std::vector<Graphite::FinishedFileOperation> finished = operations.TakeFinished();

	REQUIRE(finished.size() == 1);
	REQUIRE_FALSE(finished[0].CopyOrMovedPath.HasValue());
	CHECK(finished[0].CopyOrMovedPath.GetError().Message.find("the disk went away") != std::string::npos);
	CHECK_FALSE(operations.IsBusy(project));
	CHECK_FALSE(operations.IsRunning());
}
