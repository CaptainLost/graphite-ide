#include <doctest/doctest.h>

#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileSync.h"
#include "Core/Text/TextDocument.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

using namespace Core;

namespace {
	void WriteBytes(const std::filesystem::path& path, std::string_view contents)
	{
		std::ofstream stream{path, std::ios::binary};
		stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	}

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

// Every "the file changed underneath us" case below rewrites a file to a different length
// as well as different content. ChangedOnDisk compares the modification time and the size,
// and two writes in quick succession can land in the same clock tick — a test leaning on
// the timestamp alone is a test that eventually flickers.

TEST_CASE("A file deleted while it is open is marked missing and keeps its content")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "a.txt";
	WriteBytes(path, "still here\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Opened);

	Remove(path);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);

	// The whole reason the item stays: closing it would be the only way to lose content
	// we still have.
	REQUIRE(documents.At(0).Document.has_value());
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "still here\n");
}

TEST_CASE("A file that comes back changed is noticed in the same pass that finds it back")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "a.txt";
	WriteBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Opened);

	Remove(path);
	REQUIRE(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	// Deleted and restored with different content — git checkout after a git rm.
	WriteBytes(path, "restored from somewhere else\n");

	// Not CameBack: the sync that notices the file is back goes on to compare its
	// content in the same pass, deliberately, because this is the moment that comparison
	// is worth most. Were it to stop at clearing the mark, the tab would show the old
	// content until some unrelated later sweep happened to look.
	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::SafeToReload);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	REQUIRE(documents.At(0).Document.has_value());

	// Still the old content: SyncWithFile says a reload is due and leaves the reload to
	// the caller, which is the whole of what SafeToReload means.
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "original\n");

	REQUIRE(ReloadItem(documents, 0));
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "restored from somewhere else\n");
}

TEST_CASE("A tab opened on a file that was not there asks for the file to be loaded")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "not-yet.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Failed);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	WriteBytes(path, "here now\n");

	// The one shape in which this outcome is returned: the file is there and the item
	// has no content to compare it against, so there is nothing to reload and everything
	// still to load. Handed back rather than done, because loading is what asks about a
	// large file, and only a caller with a user in front of it can ask.
	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::NeedsLoad);
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK_FALSE(documents.At(0).Document.has_value());
}

TEST_CASE("A file that is still not there is not reported as needing loading")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "still-missing.txt";

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Failed);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::Unchanged);
	CHECK(documents.At(0).Failure == OpenFailure::Missing);
}

TEST_CASE("A file changed under a document with unsaved edits is left for the caller to decide")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "a.txt";
	WriteBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Opened);
	REQUIRE(documents.At(0).Document.has_value());

	documents.At(0).Document->GetBuffer().Insert(0, "mine ");
	REQUIRE(documents.At(0).Document->IsModified());

	WriteBytes(path, "somebody else's version\n");

	// Both versions are somebody's work, so nothing at all happens here: the in-memory
	// content is untouched, and which one survives is a question only a caller with a
	// user in front of it can ask.
	CHECK(SyncWithFile(documents, 0) == FileSyncOutcome::NeedsDecision);
	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "mine original\n");
	CHECK(documents.At(0).Document->IsModified());
}

TEST_CASE("Saving a document whose file was deleted puts the file back and clears the mark")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "a.txt";
	WriteBytes(path, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Opened);

	documents.At(0).Document->GetBuffer().Insert(0, "edited ");

	Remove(path);
	REQUIRE(SyncWithFile(documents, 0) == FileSyncOutcome::WentMissing);
	REQUIRE(documents.At(0).Failure == OpenFailure::Missing);

	REQUIRE(SaveDocument(documents, 0));

	// The write recreated the file, so the tab has no business still saying "(deleted)".
	CHECK(documents.At(0).Failure == OpenFailure::None);
	CHECK(ReadBytes(path) == "edited original\n");
}

TEST_CASE("Save all skips a document that changed on disk and keeps saving the rest")
{
	TempDirectory directory;
	const auto stale = directory.GetPath() / "stale.txt";
	const auto clean = directory.GetPath() / "clean.txt";
	WriteBytes(stale, "original\n");
	WriteBytes(clean, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(stale, true) == OpenOutcome::Opened);
	REQUIRE(documents.Open(clean, true) == OpenOutcome::Opened);

	documents.At(0).Document->GetBuffer().Insert(0, "edited ");
	documents.At(1).Document->GetBuffer().Insert(0, "edited ");

	WriteBytes(stale, "changed by somebody else\n");

	const SaveAllReport report = SaveAllModified(documents);

	// One stale file among the dirty ones is no reason to leave the others unsaved, and
	// no reason to overwrite it silently either.
	CHECK(report.Saved == 1);
	CHECK_FALSE(report.StoppedAt.has_value());
	REQUIRE(report.Conflicted.size() == 1);
	CHECK(report.Conflicted[0] == stale);
	CHECK_FALSE(report.IsComplete());

	CHECK(documents.At(0).Document->IsModified());
	CHECK(ReadBytes(stale) == "changed by somebody else\n");

	CHECK_FALSE(documents.At(1).Document->IsModified());
	CHECK(ReadBytes(clean) == "edited original\n");
}

TEST_CASE("Save all stops at a write that fails rather than carrying on past it")
{
	TempDirectory directory;
	const auto folder = directory.MakeDirectory("folder");
	const auto doomed = folder / "doomed.txt";
	const auto after = directory.GetPath() / "after.txt";
	WriteBytes(doomed, "original\n");
	WriteBytes(after, "original\n");

	DocumentSet documents;
	REQUIRE(documents.Open(doomed, true) == OpenOutcome::Opened);
	REQUIRE(documents.Open(after, true) == OpenOutcome::Opened);

	documents.At(0).Document->GetBuffer().Insert(0, "edited ");
	documents.At(1).Document->GetBuffer().Insert(0, "edited ");

	// The directory holding the first document goes away, so its write has nowhere to
	// land: a real I/O failure arranged the same way AtomicWriteTests arranges one, with
	// no permissions to change and no handle to hold open, so it behaves the same on
	// every platform this builds for. ChangedOnDisk answers false here rather than true —
	// the stat fails, and a failed stat is never read as an answer — so this document
	// reaches the write instead of being collected as a conflict, which is exactly the
	// path under test.
	std::error_code removeError;
	std::filesystem::remove_all(folder, removeError);
	REQUIRE_FALSE(removeError);

	const SaveAllReport report = SaveAllModified(documents);

	CHECK(report.Saved == 0);
	CHECK(report.Conflicted.empty());
	REQUIRE(report.StoppedAt.has_value());
	CHECK(report.StoppedAt->Path == doomed);
	CHECK_FALSE(report.StoppedAt->Error.Message.empty());
	CHECK_FALSE(report.IsComplete());

	// Stopped, not merely reported: the second document is still dirty and its file still
	// holds what it held.
	CHECK(documents.At(1).Document->IsModified());
	CHECK(ReadBytes(after) == "original\n");
}

TEST_CASE("Reloading a file that got shorter pulls the caret back into it")
{
	TempDirectory directory;
	const auto path = directory.GetPath() / "a.txt";
	WriteBytes(path, "aaaaaaaaaa\nbbbbbbbbbbbbbbbbbbbb\n");

	DocumentSet documents;
	REQUIRE(documents.Open(path, true) == OpenOutcome::Opened);
	REQUIRE(documents.At(0).Document.has_value());
	REQUIRE(documents.At(0).WidestColumn == 20);
	REQUIRE(documents.At(0).WidestLine == 1);

	// At the very end of the old content, which the new content does not reach.
	documents.At(0).Caret.Offset = documents.At(0).Document->GetBuffer().Materialize().size();
	REQUIRE(documents.At(0).Caret.Offset == 32);

	WriteBytes(path, "ab\n");

	REQUIRE(ReloadItem(documents, 0));

	CHECK(documents.At(0).Document->GetBuffer().Materialize() == "ab\n");

	// Clamped to the new length rather than left pointing past it.
	CHECK(documents.At(0).Caret.Offset == 3);
	CHECK(documents.At(0).Caret.Anchor == 3);

	// And the caches rebuilt with it: a widestColumn still reading 20 would offer a
	// horizontal scroll range with nothing in it.
	CHECK(documents.At(0).WidestColumn == 2);
	CHECK(documents.At(0).WidestLine == 0);
	CHECK(documents.At(0).IndentDepths.size() == documents.At(0).Document->GetBuffer().GetLineCount());
}

// Not covered here, and deliberately so.
//
// "A failed stat is not an answer" — SyncWithFile refuses to read an error from
// std::filesystem::exists as "the file is gone", so a share that blinked or a directory
// that briefly denied access cannot turn every document open on it into a deleted one.
// There is no portable way for a test to make exists() fail: it would need a filesystem
// that can be taken away mid-run, and every trick that comes close (a handle held with no
// sharing, an ACL edit) is Windows-only and provokes a *different* failure. A test that
// arranged some other error and called it this one would be a test named after a rule it
// does not exercise, which is worse than an admitted gap.
