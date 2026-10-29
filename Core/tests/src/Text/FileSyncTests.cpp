#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/FileSync.h"
#include "Core/Text/TextDocument.h"

#include "DeferredJobs.h"
#include "FileFixtures.h"
#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace Core;

// Every file changed underneath a document is rewritten to a different length as well as different
// content: two writes in quick succession can share a timestamp, and CheckChangedOnDisk also compares the size.

namespace {
	std::string ReadBytes(const std::filesystem::path& path)
	{
		std::ifstream stream{path, std::ios::binary};

		return std::string{std::istreambuf_iterator<char>{stream},
						   std::istreambuf_iterator<char>{}};
	}

	void Remove(const std::filesystem::path& path)
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}
}

TEST_CASE("A file deleted while it is open is marked missing and keeps its content")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "still here\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	Remove(path);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);

	REQUIRE(documents.At(0).Document != nullptr);
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "still here\n");
}

TEST_CASE("A file that comes back changed is noticed in the same pass that finds it back and its reload is left to the caller")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	Remove(path);
	REQUIRE(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	WriteFileBytes(path, "restored from somewhere else\n");

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::SafeToReload);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	REQUIRE(documents.At(0).Document != nullptr);

	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "original\n");

	REQUIRE(ReloadItem(documents, 0));
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "restored from somewhere else\n");
}

TEST_CASE("A tab opened on a file that was not there asks its caller to load the file once it appears")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "not-yet.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	WriteFileBytes(path, "here now\n");

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::NeedsLoad);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK_FALSE(documents.At(0).Document != nullptr);
}

TEST_CASE("A tab whose file is still being read is not asked to load it again when the file comes back")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	WriteFileBytes(path, std::string(static_cast<std::size_t>(BackgroundLoadThreshold) + 1, 'a'));

	DeferredJobs jobs;
	DocumentSet documents;
	documents.LoadLargeFilesWith(jobs.Submitter());
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Loading);

	documents.MarkMissing(path);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::Unchanged);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(IsLoading(documents.At(0)));
}

TEST_CASE("A hex tab has no document either but is not asked to load")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "object.obj";
	WriteFileBytes(path, std::string_view{"MZ\0\0binary", 10});

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.At(0).View == ItemView::Hex);
	REQUIRE_FALSE(documents.At(0).Document != nullptr);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::Unchanged);
	CHECK(documents.At(0).Failure == OpenFailure::None);
}

TEST_CASE("A file that is still not there is not reported as needing loading")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "still-missing.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Failed);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::Unchanged);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
}

TEST_CASE("A file changed under a document with unsaved edits is left for the caller to decide")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.At(0).Document != nullptr);

	const std::size_t startOfDocument = 0;
	documents.At(0).Document->GetBuffer().Insert(startOfDocument, "mine ");
	REQUIRE(documents.At(0).Document->IsModified());

	WriteFileBytes(path, "somebody else's version\n");

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::NeedsDecision);
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "mine original\n");
	CHECK(documents.At(0).Document->IsModified());
}

TEST_CASE("Saving a document whose file was deleted puts the file back and clears the mark")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);

	const std::size_t startOfDocument = 0;
	documents.At(0).Document->GetBuffer().Insert(startOfDocument, "edited ");

	Remove(path);
	REQUIRE(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	REQUIRE(SaveDocument(documents, 0));

	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(ReadBytes(path) == "edited original\n");
}

TEST_CASE("Save all skips a document that changed on disk rather than overwrite it and keeps saving the rest")
{
	TempDirectory directory;
	const std::filesystem::path stale = directory.GetPath() / "stale.txt";
	const std::filesystem::path clean = directory.GetPath() / "clean.txt";
	WriteFileBytes(stale, "original\n");
	WriteFileBytes(clean, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(stale, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.Open(clean, TabKind::Permanent) == OpenOutcome::Opened);
	const std::size_t staleItemIndex = 0;
	const std::size_t cleanItemIndex = 1;

	const std::size_t startOfDocument = 0;
	documents.At(staleItemIndex).Document->GetBuffer().Insert(startOfDocument, "edited ");
	documents.At(cleanItemIndex).Document->GetBuffer().Insert(startOfDocument, "edited ");

	WriteFileBytes(stale, "changed by somebody else\n");

	const SaveAllReport report = SaveAllModified(documents);

	CHECK(report.Saved == std::vector<std::filesystem::path>{clean});
	CHECK_FALSE(report.StoppedAt.has_value());
	REQUIRE(report.Conflicted.size() == 1);
	CHECK(report.Conflicted[0] == stale);
	CHECK_FALSE(report.IsComplete());

	CHECK(documents.At(staleItemIndex).Document->IsModified());
	CHECK(ReadBytes(stale) == "changed by somebody else\n");

	CHECK_FALSE(documents.At(cleanItemIndex).Document->IsModified());
	CHECK(ReadBytes(clean) == "edited original\n");
}

TEST_CASE("Save all stops at a write that fails rather than carrying on past it")
{
	TempDirectory directory;
	const std::filesystem::path folderRemovedBeforeTheSave = directory.MakeDirectory("folder");
	const std::filesystem::path doomed = folderRemovedBeforeTheSave / "doomed.txt";
	const std::filesystem::path untouched = directory.GetPath() / "after.txt";
	WriteFileBytes(doomed, "original\n");
	WriteFileBytes(untouched, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(doomed, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.Open(untouched, TabKind::Permanent) == OpenOutcome::Opened);
	const std::size_t doomedItemIndex = 0;
	const std::size_t untouchedItemIndex = 1;

	const std::size_t startOfDocument = 0;
	documents.At(doomedItemIndex).Document->GetBuffer().Insert(startOfDocument, "edited ");
	documents.At(untouchedItemIndex).Document->GetBuffer().Insert(startOfDocument, "edited ");

	// Removing the folder fails the write the same way on every platform; a read-only flag or a held handle would not.
	std::error_code removeError;
	std::filesystem::remove_all(folderRemovedBeforeTheSave, removeError);
	REQUIRE_FALSE(removeError);

	REQUIRE_FALSE(documents.At(doomedItemIndex).Document->CheckChangedOnDisk());

	const SaveAllReport report = SaveAllModified(documents);

	CHECK(report.Saved.empty());
	CHECK(report.Conflicted.empty());
	REQUIRE(report.StoppedAt.has_value());
	CHECK(report.StoppedAt->Path == doomed);
	CHECK_FALSE(report.StoppedAt->Error.Message.empty());
	CHECK_FALSE(report.IsComplete());

	CHECK(documents.At(untouchedItemIndex).Document->IsModified());
	CHECK(ReadBytes(untouched) == "original\n");
}

TEST_CASE("Reloading a file that got shorter pulls the caret back into it and rebuilds the line caches")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.txt";
	WriteFileBytes(path, "aaaaaaaaaa\nbbbbbbbbbbbbbbbbbbbb\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, TabKind::Permanent) == OpenOutcome::Opened);
	REQUIRE(documents.At(0).Document != nullptr);
	REQUIRE(documents.At(0).WidestColumn == 20);
	REQUIRE(documents.At(0).WidestLine == 1);

	const std::size_t endOfOldContent = documents.At(0).Document->GetBuffer().Materialize().size();
	documents.At(0).Caret.Offset = endOfOldContent;
	REQUIRE(documents.At(0).Caret.Offset == 32);

	WriteFileBytes(path, "ab\n");

	REQUIRE(ReloadItem(documents, 0));

	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "ab\n");

	CHECK(documents.At(0).Caret.Offset == 3);
	CHECK(documents.At(0).Caret.Anchor == 3);

	CHECK(documents.At(0).WidestColumn == 2);
	CHECK(documents.At(0).WidestLine == 0);
	CHECK(documents.At(0).IndentDepths.size() == documents.At(0).Document->GetBuffer().GetLineCount());
}
