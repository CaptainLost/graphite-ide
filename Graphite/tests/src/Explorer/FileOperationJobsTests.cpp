#include <doctest/doctest.h>

#include "Explorer/FileOperationJobs.h"

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Core/Base/Pointers.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	using Clock = Graphite::FileOperationJobs::Clock;

	constexpr std::chrono::milliseconds kWithinTheLoaderWait{100};
	constexpr std::chrono::milliseconds kPastTheLoaderWait{250};

	const Clock::time_point kStart{};

	Graphite::RunningFileOperation RunningSince(Graphite::FileOperationKind kind, const std::filesystem::path& target,
												Clock::time_point startedAt, std::size_t copiedFiles)
	{
		return Graphite::RunningFileOperation{.Kind = kind,
											  .Target = target,
											  .StartedAt = startedAt,
											  .CopiedFiles = Core::CreateRef<std::atomic<std::size_t>>(copiedFiles)};
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

TEST_CASE("The status bar says nothing of an operation for the loader's first fifth of a second")
{
	const std::vector<Graphite::RunningFileOperation> running{
		RunningSince(Graphite::FileOperationKind::Duplicate, "C:/work/Foo", kStart, 12)};

	CHECK_FALSE(Graphite::FileOperationsStatusText(running, kStart + kWithinTheLoaderWait).has_value());

	const std::optional<std::string> text = Graphite::FileOperationsStatusText(running, kStart + kPastTheLoaderWait);
	REQUIRE(text.has_value());
	CHECK(*text == "Duplicating Foo\xE2\x80\xA6 12 files");
}

TEST_CASE("The status bar names the oldest operation it shows and counts the others")
{
	const std::vector<Graphite::RunningFileOperation> running{
		RunningSince(Graphite::FileOperationKind::MoveToRecycleBin, "C:/work/Old", kStart, 0),
		RunningSince(Graphite::FileOperationKind::Duplicate, "C:/work/Middle", kStart + kWithinTheLoaderWait, 0),
		RunningSince(Graphite::FileOperationKind::Duplicate, "C:/work/New", kStart + kPastTheLoaderWait, 0)};

	constexpr std::chrono::milliseconds kOnlyTheFirstTwoShow{320};
	const std::optional<std::string> text = Graphite::FileOperationsStatusText(running, kStart + kOnlyTheFirstTwoShow);

	REQUIRE(text.has_value());
	CHECK(*text == "Moving Old to the Recycle Bin\xE2\x80\xA6 and 1 more");
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
	CHECK(finished[0].Folder == folder.GetPath());
	REQUIRE(finished[0].Outcome.HasValue());
	CHECK(finished[0].Outcome.GetValue() == folder.GetPath() / "project copy");
	CHECK(std::filesystem::exists(folder.GetPath() / "project copy" / "main.cpp"));

	CHECK_FALSE(operations.IsBusy(project));
	CHECK_FALSE(operations.IsRunning());
}

TEST_CASE("A duplicate counts the files it has copied, which the status bar reads while it runs")
{
	TempDirectory folder;
	const std::filesystem::path project = folder.MakeDirectory("project");
	folder.MakeFile("project/a.txt", "a");
	folder.MakeFile("project/b.txt", "b");

	DeferredJobs jobs;
	Graphite::FileOperationJobs operations{jobs.Submitter()};

	operations.Start(Graphite::FileOperationKind::Duplicate, project, folder.GetPath(), kStart);

	const std::optional<std::string> beforeCopying = operations.StatusText(kStart + kPastTheLoaderWait);
	REQUIRE(beforeCopying.has_value());
	CHECK(*beforeCopying == "Duplicating project\xE2\x80\xA6");

	jobs.RunAll();

	const std::optional<std::string> afterCopying = operations.StatusText(kStart + kPastTheLoaderWait);
	REQUIRE(afterCopying.has_value());
	CHECK(*afterCopying == "Duplicating project\xE2\x80\xA6 2 files");
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
	REQUIRE_FALSE(finished[0].Outcome.HasValue());
	CHECK(finished[0].Outcome.GetError().Code == Core::ErrorCode::NotFound);
}
