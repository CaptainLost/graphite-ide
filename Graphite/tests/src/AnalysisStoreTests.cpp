#include <doctest/doctest.h>

#include "Lang/Analysis/AnalysisStore.h"

#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Graphite;

namespace {

	CachedAnalysis WithDeclaration(std::string name, std::filesystem::file_time_type modifiedAt)
	{
		CachedAnalysis analysis;

		Declaration declaration;
		declaration.Name = std::move(name);
		declaration.Kind = DeclarationKind::Function;

		analysis.Declarations.push_back(std::move(declaration));
		analysis.ModifiedAt = modifiedAt;

		return analysis;
	}

	// The store writes on a worker, so a test that asserts on the disk has to wait for it. Bounded
	// rather than open-ended: a hang here should fail the case, not the suite.
	void WaitForWrites(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsQuiet(); ++attempt)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsQuiet());
	}

	// Drives SetRoot's listing to completion the way the application does — by polling.
	void WaitUntilReady(AnalysisStore& store)
	{
		for (int attempt = 0; attempt < 200 && !store.IsReady(); ++attempt)
		{
			store.Poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		REQUIRE(store.IsReady());
	}

	std::filesystem::path StoreDirectory(const TempDirectory& temp)
	{
		return temp.GetPath() / ".graphite" / "analysis";
	}

	// What the store may never leave behind: a record nothing can reach. The index is the only
	// thing that turns a path into a record's filename, so a record it does not name is invisible
	// to every later read and every later prune alike.
	std::size_t CountRecords(const std::filesystem::path& directory)
	{
		std::size_t records = 0;

		for (const auto& entry : std::filesystem::directory_iterator{directory})
		{
			if (entry.path().extension() == ".rec")
			{
				++records;
			}
		}

		return records;
	}
}

TEST_CASE("An analysis written in one session is read back in the next")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);

		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK(next.Has(source));

	const std::optional<CachedAnalysis> back = next.Read(source, stamp);

	REQUIRE(back.has_value());
	REQUIRE(back->Declarations.size() == 1);
	CHECK(back->Declarations[0].Name == "Draw");
}

TEST_CASE("An analysis of a file that has changed since is neither offered nor kept")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	// The file moves on between sessions, as it would across a branch switch.
	std::filesystem::last_write_time(source, stamp + std::chrono::seconds(60));

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));
	CHECK_FALSE(next.Read(source, std::filesystem::last_write_time(source)).has_value());
}

TEST_CASE("An analysis of a file that is gone is pruned from the disk, not left behind")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	std::filesystem::remove(source);

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));

	// Nothing may be left holding bytes for a file that no longer exists — the store has no size
	// cap, so an orphan is an orphan for the life of the project.
	std::size_t records = 0;

	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath() / ".graphite" / "analysis"})
	{
		if (entry.path().extension() == ".rec")
		{
			++records;
		}
	}

	CHECK(records == 0);
}

TEST_CASE("A corrupt index throws the whole store away rather than half-trusting it")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	{
		std::ofstream index{temp.GetPath() / ".graphite" / "analysis" / "index.bin",
							std::ios::binary | std::ios::trunc};
		index << "not an index";
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));

	// "Throws the whole store away" has to mean the disk, not just the map: an index that cannot
	// be decoded leaves records named by a hash nothing can turn back into a path, so anything
	// still sitting there is unreachable for the life of the project.
	CHECK(CountRecords(StoreDirectory(temp)) == 0);
}

TEST_CASE("An index written against another record format is discarded whole, not one record at a time")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	// The index carries the record version it was written against, in the third field after the
	// magic and its own version. Moving it stands in for a build whose record format has changed:
	// the records beside it still stat fine and would survive pruning, and each would then be
	// refused by Read one opened document at a time.
	{
		const std::filesystem::path index = StoreDirectory(temp) / "index.bin";

		std::fstream bytes{index, std::ios::binary | std::ios::in | std::ios::out};
		REQUIRE(bytes.is_open());

		bytes.seekp(8);
		bytes.put(static_cast<char>(0xEE));
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	CHECK_FALSE(next.Has(source));
	CHECK(CountRecords(StoreDirectory(temp)) == 0);
}

TEST_CASE("A record the index no longer names is swept rather than left to sit there forever")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForWrites(store);
	}

	// A record from a write whose index rewrite never landed — the process was killed in between,
	// or the index write failed. Walking the index, which is all pruning does, cannot see it.
	{
		std::ofstream orphan{StoreDirectory(temp) / "0123456789abcdef.rec", std::ios::binary};
		orphan << "whatever a previous session left";
	}

	REQUIRE(CountRecords(StoreDirectory(temp)) == 2);

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	// The live one survives; the unreachable one does not.
	CHECK(next.Has(source));
	CHECK(CountRecords(StoreDirectory(temp)) == 1);
}

TEST_CASE("A store with no root is ready at once and answers nothing")
{
	AnalysisStore store;

	CHECK(store.IsReady());
	CHECK_FALSE(store.Has("a.cpp"));
	CHECK_FALSE(store.Read("a.cpp", std::filesystem::file_time_type{}).has_value());

	// Writing without a root must be a no-op rather than a path built from an empty root.
	store.Write("a.cpp", CachedAnalysis{});
	CHECK(store.IsQuiet());
}

TEST_CASE("A record whose bytes were corrupted is refused and the entry does not survive")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitUntilReady(store);
	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForWrites(store);

	for (const auto& entry : std::filesystem::directory_iterator{temp.GetPath() / ".graphite" / "analysis"})
	{
		if (entry.path().extension() == ".rec")
		{
			std::ofstream record{entry.path(), std::ios::binary | std::ios::trunc};
			record << "rubbish";
		}
	}

	CHECK_FALSE(store.Read(source, stamp).has_value());
}

TEST_CASE("A record refused by Read is deleted rather than read again on every later open")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitUntilReady(store);
	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForWrites(store);

	for (const auto& entry : std::filesystem::directory_iterator{StoreDirectory(temp)})
	{
		if (entry.path().extension() == ".rec")
		{
			std::ofstream record{entry.path(), std::ios::binary | std::ios::trunc};
			record << "rubbish";
		}
	}

	CHECK_FALSE(store.Read(source, stamp).has_value());
	CHECK_FALSE(store.Has(source));

	// The deletion is handed to the worker, so the disk catches up a moment later — an entry
	// nothing will ever accept is one every later open would otherwise pay to read again.
	WaitForWrites(store);

	CHECK(CountRecords(StoreDirectory(temp)) == 0);
}

namespace {

	// How many entries the index on disk claims. The count sits after the magic and the two
	// version fields, all four bytes each and little endian — see AnalysisRecord.h.
	std::size_t IndexEntryCount(const std::filesystem::path& directory)
	{
		std::ifstream index{directory / "index.bin", std::ios::binary};
		REQUIRE(index.is_open());

		char bytes[16] = {};
		index.read(bytes, sizeof(bytes));

		std::size_t count = 0;

		for (int byte = 3; byte >= 0; --byte)
		{
			count = (count << 8) | static_cast<unsigned char>(bytes[12 + byte]);
		}

		return count;
	}
}

TEST_CASE("The index is not rewritten once per record, and a clean exit still names them all")
{
	TempDirectory temp;

	std::vector<std::filesystem::path> sources;
	std::vector<std::filesystem::file_time_type> stamps;

	for (int file = 0; file < 5; ++file)
	{
		const std::filesystem::path source =
			temp.MakeFile("f" + std::to_string(file) + ".cpp", "int x;");

		sources.push_back(source);
		stamps.push_back(std::filesystem::last_write_time(source));
	}

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitUntilReady(store);

		for (std::size_t file = 0; file < sources.size(); ++file)
		{
			store.Write(sources[file], WithDeclaration("Draw", stamps[file]));
		}

		WaitForWrites(store);

		// Every record is on the disk, and the index still names none of them: rewriting it with
		// each one is what makes the bytes written over a first pass quadratic in the size of the
		// workspace, and a handful of records is nowhere near worth one.
		CHECK(CountRecords(StoreDirectory(temp)) == sources.size());
		CHECK(IndexEntryCount(StoreDirectory(temp)) == 0);
	}

	// The closing store flushes what had coalesced, so a clean exit loses nothing.
	CHECK(IndexEntryCount(StoreDirectory(temp)) == sources.size());

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitUntilReady(next);

	for (std::size_t file = 0; file < sources.size(); ++file)
	{
		CHECK(next.Has(sources[file]));
		CHECK(next.Read(sources[file], stamps[file]).has_value());
	}

	CHECK(CountRecords(StoreDirectory(temp)) == sources.size());
}

TEST_CASE("Pointing the store at another folder writes the index the last one had coalesced")
{
	TempDirectory first;
	TempDirectory second;

	const std::filesystem::path source = first.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	AnalysisStore store;
	store.SetRoot(first.GetPath());
	WaitUntilReady(store);

	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForWrites(store);

	REQUIRE(IndexEntryCount(StoreDirectory(first)) == 0);

	// A folder closing is as much an end for that root as the process exiting is.
	store.SetRoot(second.GetPath());
	WaitUntilReady(store);
	WaitForWrites(store);

	CHECK(IndexEntryCount(StoreDirectory(first)) == 1);
}
