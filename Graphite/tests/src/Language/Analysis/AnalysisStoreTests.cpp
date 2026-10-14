#include <doctest/doctest.h>

#include "Language/Analysis/AnalysisStore.h"

#include "AnalysisFixtures.h"
#include "TempDirectory.h"

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	constexpr std::streamoff kIndexRecordVersionAt = 8;
	constexpr std::size_t kIndexHeaderBytes = 16;
	constexpr std::size_t kIndexEntryCountAt = 12;
	constexpr int kBytesPerCount = 4;

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

	std::filesystem::path StoreDirectory(const TempDirectory& temp)
	{
		return temp.GetPath() / ".graphite" / "analysis";
	}

	std::size_t CountRecords(const std::filesystem::path& directory)
	{
		std::size_t records = 0;

		for (const std::filesystem::directory_entry& directoryEntry :
			 std::filesystem::directory_iterator{directory})
		{
			if (directoryEntry.path().extension() == ".rec")
			{
				++records;
			}
		}

		return records;
	}

	void OverwriteRecordsWithRubbish(const std::filesystem::path& directory)
	{
		for (const std::filesystem::directory_entry& directoryEntry :
			 std::filesystem::directory_iterator{directory})
		{
			if (directoryEntry.path().extension() == ".rec")
			{
				std::ofstream record{directoryEntry.path(), std::ios::binary | std::ios::trunc};
				record << "rubbish";
			}
		}
	}

	std::size_t IndexEntryCount(const std::filesystem::path& directory)
	{
		std::ifstream index{directory / "index.bin", std::ios::binary};
		REQUIRE(index.is_open());

		char bytes[kIndexHeaderBytes] = {};
		index.read(bytes, sizeof(bytes));

		std::size_t count = 0;

		for (int byteIndex = kBytesPerCount - 1; byteIndex >= 0; --byteIndex)
		{
			count = (count << 8) | static_cast<unsigned char>(bytes[kIndexEntryCountAt + byteIndex]);
		}

		return count;
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
		WaitForStoreReady(store);

		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

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
		WaitForStoreReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	std::filesystem::last_write_time(source, stamp + std::chrono::seconds(60));

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

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
		WaitForStoreReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	std::filesystem::remove(source);

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

	CHECK_FALSE(next.Has(source));

	CHECK(CountRecords(StoreDirectory(temp)) == 0);
}

TEST_CASE("A corrupt index throws the whole store away rather than half-trusting it")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	{
		AnalysisStore store;
		store.SetRoot(temp.GetPath());
		WaitForStoreReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	{
		std::ofstream index{StoreDirectory(temp) / "index.bin", std::ios::binary | std::ios::trunc};
		index << "not an index";
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

	CHECK_FALSE(next.Has(source));

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
		WaitForStoreReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	{
		const std::filesystem::path index = StoreDirectory(temp) / "index.bin";

		std::fstream bytes{index, std::ios::binary | std::ios::in | std::ios::out};
		REQUIRE(bytes.is_open());

		bytes.seekp(kIndexRecordVersionAt);
		bytes.put(static_cast<char>(0xEE));
	}

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

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
		WaitForStoreReady(store);
		store.Write(source, WithDeclaration("Draw", stamp));
		WaitForStoreWrites(store);
	}

	{
		std::ofstream orphan{StoreDirectory(temp) / "0123456789abcdef.rec", std::ios::binary};
		orphan << "whatever a previous session left";
	}

	REQUIRE(CountRecords(StoreDirectory(temp)) == 2);

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

	CHECK(next.Has(source));
	CHECK(CountRecords(StoreDirectory(temp)) == 1);
}

TEST_CASE("A store with no root is ready at once and answers nothing")
{
	AnalysisStore store;

	CHECK(store.IsReady());
	CHECK_FALSE(store.Has("a.cpp"));
	CHECK_FALSE(store.Read("a.cpp", std::filesystem::file_time_type{}).has_value());

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
	WaitForStoreReady(store);
	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForStoreWrites(store);

	OverwriteRecordsWithRubbish(StoreDirectory(temp));

	CHECK_FALSE(store.Read(source, stamp).has_value());
}

TEST_CASE("A record refused by Read is deleted rather than read again on every later open")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");
	const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(source);

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitForStoreReady(store);
	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForStoreWrites(store);

	OverwriteRecordsWithRubbish(StoreDirectory(temp));

	CHECK_FALSE(store.Read(source, stamp).has_value());
	CHECK_FALSE(store.Has(source));

	WaitForStoreWrites(store);

	CHECK(CountRecords(StoreDirectory(temp)) == 0);
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
		WaitForStoreReady(store);

		for (std::size_t file = 0; file < sources.size(); ++file)
		{
			store.Write(sources[file], WithDeclaration("Draw", stamps[file]));
		}

		WaitForStoreWrites(store);

		CHECK(CountRecords(StoreDirectory(temp)) == sources.size());
		CHECK(IndexEntryCount(StoreDirectory(temp)) == 0);
	}

	CHECK(IndexEntryCount(StoreDirectory(temp)) == sources.size());

	AnalysisStore next;
	next.SetRoot(temp.GetPath());
	WaitForStoreReady(next);

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
	WaitForStoreReady(store);

	store.Write(source, WithDeclaration("Draw", stamp));
	WaitForStoreWrites(store);

	REQUIRE(IndexEntryCount(StoreDirectory(first)) == 0);

	store.SetRoot(second.GetPath());
	WaitForStoreReady(store);
	WaitForStoreWrites(store);

	CHECK(IndexEntryCount(StoreDirectory(first)) == 1);
}
