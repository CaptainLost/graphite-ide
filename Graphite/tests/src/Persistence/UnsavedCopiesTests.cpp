#include <doctest/doctest.h>

#include "Persistence/UnsavedCopies.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/DocumentStore.h"
#include "Core/Text/TextDocument.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using namespace Graphite;

namespace {

	Core::JobSubmitter RunAtOnce()
	{
		return [](std::function<void()> job) { job(); };
	}

	std::vector<std::filesystem::path> CopiesUnder(const std::filesystem::path& directory)
	{
		std::vector<std::filesystem::path> copies;
		std::error_code listError;

		for (std::filesystem::recursive_directory_iterator entry{directory, listError}, end; !listError && entry != end;
			 entry.increment(listError))
		{
			if (entry->path().extension() == ".copy")
			{
				copies.push_back(entry->path());
			}
		}

		return copies;
	}

	struct Folder
	{
		TempDirectory Temp;
		std::filesystem::path Copies = Temp.MakeDirectory("unsaved");
		Core::DocumentStore Store;

		std::shared_ptr<Core::TextDocument> Open(const std::string& name, const std::string& text)
		{
			const std::filesystem::path file = Temp.MakeFile(name, text);
			Core::Result<std::shared_ptr<Core::TextDocument>> document = Store.Acquire(file);
			REQUIRE(document.HasValue());
			return std::move(document).GetValue();
		}
	};
}

TEST_CASE("An unsaved copy reads back as the path and text it was written with")
{
	const std::filesystem::path path = std::filesystem::path{"D:\\project\\źródło.cpp"};
	const std::optional<UnsavedCopy> copy = DecodeUnsavedCopy(EncodeUnsavedCopy(path, "line one\nline two\n"));

	REQUIRE(copy.has_value());
	CHECK(copy->Path == path);
	CHECK(copy->Text == "line one\nline two\n");
}

TEST_CASE("Bytes that are not an unsaved copy are not read as one")
{
	CHECK_FALSE(DecodeUnsavedCopy("").has_value());
	CHECK_FALSE(DecodeUnsavedCopy("some other file\nD:\\a.txt\ntext").has_value());
	CHECK_FALSE(DecodeUnsavedCopy("graphite unsaved copy 1\n\ntext").has_value());
}

TEST_CASE("A modified document is copied once its text has rested, not while it is typed")
{
	Folder folder;
	UnsavedCopies copies{folder.Copies, RunAtOnce()};
	const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "hello");

	copies.Observe(0.0, folder.Store);
	CHECK(CopiesUnder(folder.Copies).empty());

	document->GetBuffer().Insert(5, " world");
	copies.Observe(0.5, folder.Store);
	CHECK(CopiesUnder(folder.Copies).empty());

	copies.Observe(UnsavedCopies::kRestSeconds, folder.Store);
	const std::vector<std::filesystem::path> written = CopiesUnder(folder.Copies);
	REQUIRE(written.size() == 1);

	const Core::Result<std::string> bytes = Core::ReadFileBytes(written.front());
	REQUIRE(bytes.HasValue());
	const std::optional<UnsavedCopy> copy = DecodeUnsavedCopy(bytes.GetValue());
	REQUIRE(copy.has_value());
	CHECK(copy->Path == document->GetPath());
	CHECK(copy->Text == "hello world");
}

TEST_CASE("A document typed into without a pause is still copied after the longest wait")
{
	Folder folder;
	UnsavedCopies copies{folder.Copies, RunAtOnce()};
	const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "");

	// The wait counts from the first frame the change is seen.
	document->GetBuffer().Insert(0, "x");
	copies.Observe(0.0, folder.Store);

	double typedFor = 0.0;

	while (typedFor < UnsavedCopies::kLongestUncopiedSeconds - 0.25)
	{
		document->GetBuffer().Insert(document->GetBuffer().GetLength(), "x");
		copies.Observe(0.25, folder.Store);
		typedFor += 0.25;
		REQUIRE(CopiesUnder(folder.Copies).empty());
	}

	document->GetBuffer().Insert(document->GetBuffer().GetLength(), "x");
	copies.Observe(0.25, folder.Store);
	CHECK(CopiesUnder(folder.Copies).size() == 1);
}

TEST_CASE("A copy goes once its document is saved or undone back to the file or closed")
{
	Folder folder;
	UnsavedCopies copies{folder.Copies, RunAtOnce()};
	std::shared_ptr<Core::TextDocument> saved = folder.Open("saved.txt", "a");
	std::shared_ptr<Core::TextDocument> undone = folder.Open("undone.txt", "b");
	std::shared_ptr<Core::TextDocument> closed = folder.Open("closed.txt", "c");

	for (const std::shared_ptr<Core::TextDocument>& document : {saved, undone, closed})
	{
		document->GetBuffer().Insert(0, "edited ");
	}

	copies.Observe(0.0, folder.Store);
	copies.Observe(UnsavedCopies::kRestSeconds, folder.Store);
	REQUIRE(CopiesUnder(folder.Copies).size() == 3);

	REQUIRE(saved->Save().HasValue());
	copies.Observe(0.1, folder.Store);
	CHECK(CopiesUnder(folder.Copies).size() == 2);

	undone->GetBuffer().Undo();
	REQUIRE_FALSE(undone->IsModified());
	copies.Observe(0.1, folder.Store);
	CHECK(CopiesUnder(folder.Copies).size() == 1);

	closed.reset();
	copies.Observe(0.1, folder.Store);
	CHECK(CopiesUnder(folder.Copies).empty());
}

TEST_CASE("The copies of a launch still running are not claimed")
{
	Folder folder;
	UnsavedCopies running{folder.Copies, RunAtOnce()};
	const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "a");
	document->GetBuffer().Insert(0, "b");
	running.Observe(0.0, folder.Store);
	running.Observe(UnsavedCopies::kRestSeconds, folder.Store);
	REQUIRE(CopiesUnder(folder.Copies).size() == 1);

	UnsavedCopies next{folder.Copies, RunAtOnce()};
	CHECK(next.ClaimLeftBehind().empty());
}

TEST_CASE("A launch that died leaves its copies to the next one which keeps them until it has copied them again")
{
	Folder folder;
	std::filesystem::path path;

	{
		UnsavedCopies crashed{folder.Copies, RunAtOnce()};
		const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "on disk");
		path = document->GetPath();
		document->GetBuffer().Insert(0, "typed ");
		crashed.Observe(0.0, folder.Store);
		crashed.Observe(UnsavedCopies::kRestSeconds, folder.Store);
		// Destroyed without Discard, as a killed process lets go of its lock and nothing else.
	}

	REQUIRE(CopiesUnder(folder.Copies).size() == 1);

	UnsavedCopies next{folder.Copies, RunAtOnce()};
	const std::vector<UnsavedCopy> claimed = next.ClaimLeftBehind();
	REQUIRE(claimed.size() == 1);
	CHECK(claimed.front().Path == path);
	CHECK(claimed.front().Text == "typed on disk");

	PutBackUnsaved putBack = PutBack(folder.Store, claimed);
	REQUIRE(putBack.Documents.size() == 1);
	CHECK(putBack.Failures.empty());

	const std::shared_ptr<Core::TextDocument>& restored = putBack.Documents.front();
	CHECK(restored->IsModified());
	CHECK(restored->GetBuffer().Materialize() == "typed on disk");

	// Until this launch has its own copy, the claimed one is all there is.
	next.Observe(0.0, folder.Store);
	CHECK(CopiesUnder(folder.Copies).size() == 1);

	next.Observe(UnsavedCopies::kRestSeconds, folder.Store);
	const std::vector<std::filesystem::path> after = CopiesUnder(folder.Copies);
	REQUIRE(after.size() == 1);
	CHECK(after.front() != claimed.front().KeptIn);
	CHECK_FALSE(std::filesystem::exists(claimed.front().KeptIn.parent_path()));

	restored->GetBuffer().Undo();
	CHECK(restored->GetBuffer().Materialize() == "on disk");
	CHECK_FALSE(restored->IsModified());
}

TEST_CASE("A claimed copy whose file is gone is kept for the next launch")
{
	Folder folder;
	std::filesystem::path path;

	{
		UnsavedCopies crashed{folder.Copies, RunAtOnce()};
		const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "x");
		path = document->GetPath();
		document->GetBuffer().Insert(0, "y");
		crashed.Observe(0.0, folder.Store);
		crashed.Observe(UnsavedCopies::kRestSeconds, folder.Store);
	}

	std::filesystem::remove(path);

	{
		UnsavedCopies next{folder.Copies, RunAtOnce()};
		const std::vector<UnsavedCopy> claimed = next.ClaimLeftBehind();
		REQUIRE(claimed.size() == 1);

		const PutBackUnsaved putBack = PutBack(folder.Store, claimed);
		CHECK(putBack.Documents.empty());
		CHECK(putBack.Failures.size() == 1);

		next.Observe(UnsavedCopies::kRestSeconds, folder.Store);
		next.Discard();
	}

	UnsavedCopies third{folder.Copies, RunAtOnce()};
	CHECK(third.ClaimLeftBehind().size() == 1);
}

TEST_CASE("Discarding leaves nothing of this launch for the next")
{
	Folder folder;
	const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "a");

	{
		UnsavedCopies copies{folder.Copies, RunAtOnce()};
		document->GetBuffer().Insert(0, "b");
		copies.Observe(0.0, folder.Store);
		copies.Observe(UnsavedCopies::kRestSeconds, folder.Store);
		REQUIRE(CopiesUnder(folder.Copies).size() == 1);

		copies.Discard();
	}

	CHECK(CopiesUnder(folder.Copies).empty());
	CHECK(std::filesystem::is_empty(folder.Copies));
}

TEST_CASE("Keeping for the next launch copies a document that has not rested yet")
{
	Folder folder;
	const std::shared_ptr<Core::TextDocument> document = folder.Open("a.txt", "a");

	{
		UnsavedCopies copies{folder.Copies, RunAtOnce()};
		copies.Observe(0.0, folder.Store);
		document->GetBuffer().Insert(0, "just typed ");
		copies.Observe(0.1, folder.Store);
		REQUIRE(CopiesUnder(folder.Copies).empty());

		copies.KeepForNextLaunch(folder.Store);
	}

	UnsavedCopies next{folder.Copies, RunAtOnce()};
	const std::vector<UnsavedCopy> claimed = next.ClaimLeftBehind();
	REQUIRE(claimed.size() == 1);
	CHECK(claimed.front().Text == "just typed a");
}
