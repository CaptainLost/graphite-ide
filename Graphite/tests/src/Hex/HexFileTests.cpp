#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Hex/HexFile.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <share.h>
#include <vector>

using Graphite::HexFile;
using Graphite::kHexBlockBytes;
using Graphite::kHexCachedBlocks;

namespace {

	// Every fixture is named with Polish letters, so the reads are known to reach a path the ANSI codepage cannot spell.
	std::filesystem::path FixturePath(const TempDirectory& directory)
	{
		return directory.GetPath() / std::filesystem::path(u8"zrzut pamięci łąki.bin");
	}

	// No two neighbouring bytes are the same, and a block's bytes differ from the block before it.
	std::vector<std::uint8_t> Pattern(std::size_t size)
	{
		std::vector<std::uint8_t> bytes(size);

		for (std::size_t at = 0; at < size; ++at)
		{
			bytes[at] = static_cast<std::uint8_t>((at % 251) ^ (at / kHexBlockBytes));
		}

		return bytes;
	}

	std::vector<std::uint8_t> Filled(std::size_t size, char with)
	{
		return std::vector<std::uint8_t>(size, static_cast<std::uint8_t>(with));
	}

	std::filesystem::path WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
	{
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return path;
	}

	// Moves the modified time on as well, so the change is seen even when the size stays the same.
	void Rewrite(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
	{
		const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);
		WriteFile(path, bytes);
		std::filesystem::last_write_time(path, before + std::chrono::seconds(60));
	}

	void Land(HexFile& file, DeferredJobs& jobs)
	{
		jobs.RunAll();
		file.Update();
	}

	std::size_t CachedBlocks(const HexFile& file, std::size_t blockCount)
	{
		std::size_t cached = 0;

		for (std::size_t block = 0; block < blockCount; ++block)
		{
			if (file.ByteAt(block * kHexBlockBytes).has_value())
			{
				++cached;
			}
		}

		return cached;
	}
}

TEST_CASE("A hex file reads the blocks it is asked for in the background")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::vector<std::uint8_t> bytes = Pattern(kHexBlockBytes + 100);
	const std::filesystem::path path = WriteFile(FixturePath(directory), bytes);
	REQUIRE(std::filesystem::exists(path));
	HexFile file(path, jobs.Submitter());

	CHECK(file.GetSize() == bytes.size());
	CHECK(jobs.Count() == 0);
	CHECK_FALSE(file.IsWaiting());
	CHECK_FALSE(file.ByteAt(0).has_value());
	CHECK(file.BytesFrom(0, 16).empty());

	file.Want(0, 15);
	CHECK(jobs.Count() == 1);
	CHECK(file.IsWaiting());

	file.Update();
	CHECK_FALSE(file.ByteAt(0).has_value());

	jobs.RunAll();
	CHECK_FALSE(file.ByteAt(0).has_value());
	CHECK(file.IsWaiting());

	file.Update();
	CHECK_FALSE(file.IsWaiting());
	CHECK(file.GetFailure().empty());
	CHECK(file.ByteAt(0) == bytes[0]);
	CHECK(file.ByteAt(15) == bytes[15]);

	// The whole block is read, not only the bytes asked for, and nothing of the block after it.
	CHECK(file.ByteAt(kHexBlockBytes - 1) == bytes[kHexBlockBytes - 1]);
	CHECK_FALSE(file.ByteAt(kHexBlockBytes).has_value());
	CHECK(file.BytesFrom(kHexBlockBytes - 4, 8) ==
		  std::vector<std::uint8_t>(bytes.begin() + kHexBlockBytes - 4, bytes.begin() + kHexBlockBytes));

	file.Want(kHexBlockBytes, bytes.size() - 1);
	CHECK(jobs.Count() == 1);
	Land(file, jobs);

	CHECK(file.ByteAt(bytes.size() - 1) == bytes.back());
	CHECK_FALSE(file.ByteAt(bytes.size()).has_value());
	CHECK(file.BytesFrom(kHexBlockBytes - 4, 8) ==
		  std::vector<std::uint8_t>(bytes.begin() + kHexBlockBytes - 4, bytes.begin() + kHexBlockBytes + 4));
	CHECK(file.BytesFrom(bytes.size() - 2, 10) == std::vector<std::uint8_t>(bytes.end() - 2, bytes.end()));
	CHECK(file.BytesFrom(bytes.size(), 10).empty());

	// Reading a block for the first time changes no byte that was shown.
	CHECK(file.GetChangeCount() == 0);
}

TEST_CASE("A block is asked for once while its read runs")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::vector<std::uint8_t> bytes = Pattern(2 * kHexBlockBytes);
	HexFile file(WriteFile(FixturePath(directory), bytes), jobs.Submitter());

	file.Want(0, 15);
	file.Want(0, 15);
	file.Want(100, 200);
	CHECK(jobs.Count() == 1);

	// A range across the boundary asks for the block after it as well.
	file.Want(kHexBlockBytes - 1, kHexBlockBytes);
	CHECK(jobs.Count() == 2);

	Land(file, jobs);
	CHECK_FALSE(file.IsWaiting());

	file.Want(0, bytes.size() - 1);
	CHECK(jobs.Count() == 0);

	// Past the end there is nothing to read.
	file.Want(0, 10 * kHexBlockBytes);
	file.Want(bytes.size(), bytes.size() + 100);
	CHECK(jobs.Count() == 0);
	CHECK_FALSE(file.IsWaiting());
}

TEST_CASE("Blocks past the cache size are dropped least recently wanted first")
{
	TempDirectory directory;
	DeferredJobs jobs;
	constexpr std::size_t kBlockCount = kHexCachedBlocks + 1;
	const std::vector<std::uint8_t> bytes = Pattern(kBlockCount * kHexBlockBytes);
	HexFile file(WriteFile(FixturePath(directory), bytes), jobs.Submitter());

	for (std::size_t block = 0; block < kHexCachedBlocks; ++block)
	{
		file.Want(block * kHexBlockBytes, block * kHexBlockBytes);
	}

	REQUIRE(jobs.Count() == kHexCachedBlocks);
	Land(file, jobs);
	CHECK(CachedBlocks(file, kBlockCount) == kHexCachedBlocks);

	// Block 0 is wanted again, which leaves block 1 the one wanted longest ago.
	file.Want(0, 0);
	file.Want(kHexCachedBlocks * kHexBlockBytes, kHexCachedBlocks * kHexBlockBytes);
	CHECK(jobs.Count() == 1);
	Land(file, jobs);

	CHECK(CachedBlocks(file, kBlockCount) == kHexCachedBlocks);
	CHECK(file.ByteAt(0) == bytes[0]);
	CHECK_FALSE(file.ByteAt(kHexBlockBytes).has_value());
	CHECK(file.ByteAt(2 * kHexBlockBytes) == bytes[2 * kHexBlockBytes]);
	CHECK(file.ByteAt(kHexCachedBlocks * kHexBlockBytes) == bytes[kHexCachedBlocks * kHexBlockBytes]);

	// A dropped block is read again once it is wanted.
	file.Want(kHexBlockBytes, kHexBlockBytes);
	CHECK(jobs.Count() == 1);
	Land(file, jobs);
	CHECK(file.ByteAt(kHexBlockBytes) == bytes[kHexBlockBytes]);
	CHECK(CachedBlocks(file, kBlockCount) == kHexCachedBlocks);
}

TEST_CASE("A block read after it left the screen is dropped by when it was wanted, not by when it landed")
{
	TempDirectory directory;
	DeferredJobs jobs;
	constexpr std::size_t kBlockCount = kHexCachedBlocks + 1;
	const std::vector<std::uint8_t> bytes = Pattern(kBlockCount * kHexBlockBytes);
	HexFile file(WriteFile(FixturePath(directory), bytes), jobs.Submitter());

	for (std::size_t block = 0; block + 1 < kHexCachedBlocks; ++block)
	{
		file.Want(block * kHexBlockBytes, block * kHexBlockBytes);
	}

	Land(file, jobs);

	// The last block before the cache is full is scrolled past while its read runs, and lands frames later.
	const std::size_t late = kHexCachedBlocks - 1;
	file.Want(late * kHexBlockBytes, late * kHexBlockBytes);
	file.Update();
	file.Update();
	Land(file, jobs);
	REQUIRE(CachedBlocks(file, kBlockCount) == kHexCachedBlocks);

	file.Want(kHexCachedBlocks * kHexBlockBytes, kHexCachedBlocks * kHexBlockBytes);
	Land(file, jobs);

	CHECK(file.ByteAt(late * kHexBlockBytes) == bytes[late * kHexBlockBytes]);
	CHECK_FALSE(file.ByteAt(0).has_value());
	CHECK(CachedBlocks(file, kBlockCount) == kHexCachedBlocks);
}

TEST_CASE("A changed file keeps its old bytes until the new ones are read")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::size_t size = kHexBlockBytes + 32;
	const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(size, 'A'));
	HexFile file(path, jobs.Submitter());

	// The second block is read and scrolled away from; the first is the one on screen.
	file.Want(kHexBlockBytes, size - 1);
	Land(file, jobs);
	file.Want(0, 31);
	Land(file, jobs);
	REQUIRE(file.ByteAt(0) == 'A');
	REQUIRE(file.ByteAt(kHexBlockBytes) == 'A');
	const std::uint64_t changes = file.GetChangeCount();

	file.CheckDisk();
	CHECK(jobs.Count() == 0);

	Rewrite(path, Filled(size, 'B'));
	file.CheckDisk();
	CHECK(jobs.Count() == 1);
	CHECK(file.IsWaiting());
	CHECK(file.GetSize() == size);
	CHECK(file.GetChangeCount() == changes);
	CHECK(file.ByteAt(0) == 'A');
	CHECK(file.BytesFrom(0, 32) == Filled(32, 'A'));

	// Still on screen while its new version is read, it is not asked for twice.
	file.Want(0, 31);
	CHECK(jobs.Count() == 1);

	file.Update();
	CHECK(file.ByteAt(0) == 'A');

	Land(file, jobs);
	CHECK(file.ByteAt(0) == 'B');
	CHECK(file.BytesFrom(0, 32) == Filled(32, 'B'));
	CHECK_FALSE(file.IsWaiting());
	CHECK(file.GetChangeCount() == changes + 1);

	// The block off screen keeps its old bytes until it is wanted and read again.
	CHECK(file.ByteAt(kHexBlockBytes) == 'A');
	file.Want(kHexBlockBytes, size - 1);
	CHECK(jobs.Count() == 1);
	CHECK(file.ByteAt(kHexBlockBytes) == 'A');
	Land(file, jobs);
	CHECK(file.ByteAt(kHexBlockBytes) == 'B');
	CHECK(file.GetChangeCount() == changes + 2);

	file.CheckDisk();
	CHECK(jobs.Count() == 0);
}

TEST_CASE("A file whose rows are not on screen reads nothing when it changes")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(32, 'A'));
	HexFile file(path, jobs.Submitter());
	file.Want(0, 31);
	Land(file, jobs);

	// A frame in which the tab is not drawn.
	file.Update();

	Rewrite(path, Filled(32, 'B'));
	file.CheckDisk();
	CHECK(jobs.Count() == 0);
	CHECK_FALSE(file.IsWaiting());
	CHECK(file.ByteAt(0) == 'A');

	file.Want(0, 31);
	CHECK(jobs.Count() == 1);
	Land(file, jobs);
	CHECK(file.ByteAt(0) == 'B');
}

TEST_CASE("A file that grows or shrinks reports its new size")
{
	TempDirectory directory;
	DeferredJobs jobs;

	SUBCASE("A file that grows")
	{
		const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(20, 'A'));
		HexFile file(path, jobs.Submitter());
		file.Want(0, 31);
		Land(file, jobs);
		REQUIRE(file.ByteAt(19) == 'A');
		const std::uint64_t changes = file.GetChangeCount();

		Rewrite(path, Filled(100, 'B'));
		file.CheckDisk();
		CHECK(file.GetSize() == 100);
		CHECK(file.GetChangeCount() == changes + 1);
		CHECK(jobs.Count() == 1);
		CHECK(file.ByteAt(19) == 'A');
		CHECK_FALSE(file.ByteAt(20).has_value());

		Land(file, jobs);
		CHECK(file.ByteAt(19) == 'B');
		CHECK(file.ByteAt(99) == 'B');
		CHECK_FALSE(file.ByteAt(100).has_value());
	}

	SUBCASE("A file that shrinks")
	{
		const std::size_t size = 2 * kHexBlockBytes + 10;
		const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(size, 'A'));
		HexFile file(path, jobs.Submitter());
		file.Want(0, size - 1);
		CHECK(jobs.Count() == 3);
		Land(file, jobs);
		REQUIRE(file.ByteAt(size - 1) == 'A');
		const std::uint64_t changes = file.GetChangeCount();

		Rewrite(path, Filled(100, 'B'));
		file.CheckDisk();
		CHECK(file.GetSize() == 100);
		CHECK(file.GetChangeCount() == changes + 1);

		// Nothing past the new end is read, and nothing past it is shown.
		CHECK(jobs.Count() == 1);
		CHECK(file.ByteAt(99) == 'A');
		CHECK_FALSE(file.ByteAt(100).has_value());
		CHECK_FALSE(file.ByteAt(kHexBlockBytes).has_value());
		CHECK(file.BytesFrom(90, 50) == Filled(10, 'A'));

		Land(file, jobs);
		CHECK(file.ByteAt(99) == 'B');
		CHECK_FALSE(file.IsWaiting());

		// Grown back, the file shows none of the bytes it had before it shrank.
		file.Want(0, 99);
		Rewrite(path, Filled(size, 'C'));
		file.CheckDisk();
		CHECK(file.GetSize() == size);
		CHECK(jobs.Count() == 1);
		CHECK(file.ByteAt(0) == 'B');
		CHECK_FALSE(file.ByteAt(kHexBlockBytes).has_value());
		CHECK_FALSE(file.ByteAt(size - 1).has_value());

		file.Want(0, size - 1);
		CHECK(jobs.Count() == 3);
		Land(file, jobs);
		CHECK(file.ByteAt(0) == 'C');
		CHECK(file.ByteAt(size - 1) == 'C');
	}

	SUBCASE("A file emptied")
	{
		const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(20, 'A'));
		HexFile file(path, jobs.Submitter());
		file.Want(0, 15);
		Land(file, jobs);
		const std::uint64_t changes = file.GetChangeCount();

		Rewrite(path, {});
		file.CheckDisk();
		CHECK(file.GetSize() == 0);
		CHECK(file.GetChangeCount() == changes + 1);
		CHECK(jobs.Count() == 0);
		CHECK_FALSE(file.ByteAt(0).has_value());
		CHECK(file.BytesFrom(0, 16).empty());
		CHECK_FALSE(file.IsWaiting());
	}

	SUBCASE("A file that is gone keeps the size it had")
	{
		const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(20, 'A'));
		HexFile file(path, jobs.Submitter());
		file.Want(0, 15);
		Land(file, jobs);
		const std::uint64_t changes = file.GetChangeCount();

		REQUIRE(std::filesystem::remove(path));
		file.CheckDisk();
		CHECK(file.GetSize() == 20);
		CHECK(file.GetChangeCount() == changes);
		CHECK(jobs.Count() == 0);
		CHECK(file.ByteAt(0) == 'A');
	}
}

TEST_CASE("A file held with no sharing fails with the reason and can be retried")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::vector<std::uint8_t> bytes = Pattern(100);
	const std::filesystem::path path = WriteFile(FixturePath(directory), bytes);

	SUBCASE("Held by another program")
	{
		std::FILE* held = ::_wfsopen(path.c_str(), L"rb", _SH_DENYRW);
		REQUIRE(held != nullptr);

		HexFile file(path, jobs.Submitter());
		CHECK(file.GetSize() == bytes.size());

		file.Want(0, 15);
		Land(file, jobs);
		CHECK(file.GetFailure() == "the file cannot be opened");
		CHECK_FALSE(file.ByteAt(0).has_value());
		CHECK_FALSE(file.IsWaiting());

		// A failed read is not tried again on every frame, nor by a check that finds the file unchanged.
		file.Want(0, 15);
		file.CheckDisk();
		CHECK(jobs.Count() == 0);
		CHECK(file.GetFailure() == "the file cannot be opened");

		std::fclose(held);

		file.Retry();
		CHECK(file.GetFailure().empty());
		CHECK(jobs.Count() == 1);
		CHECK(file.IsWaiting());

		Land(file, jobs);
		CHECK(file.GetFailure().empty());
		CHECK(file.ByteAt(0) == bytes[0]);
		CHECK_FALSE(file.IsWaiting());
	}

	SUBCASE("Deleted after it was opened")
	{
		HexFile file(path, jobs.Submitter());
		REQUIRE(std::filesystem::remove(path));

		file.Want(0, 15);
		Land(file, jobs);
		CHECK(file.GetFailure() == "the file was not found");

		WriteFile(path, bytes);
		file.Retry();
		CHECK(file.GetFailure().empty());
		CHECK(jobs.Count() == 1);

		Land(file, jobs);
		CHECK(file.GetFailure().empty());
		CHECK(file.ByteAt(15) == bytes[15]);
	}
}

TEST_CASE("A result of an older generation is ignored")
{
	TempDirectory directory;
	DeferredJobs jobs;
	const std::filesystem::path path = WriteFile(FixturePath(directory), Filled(32, 'A'));

	SUBCASE("A read")
	{
		HexFile file(path, jobs.Submitter());
		file.Want(0, 31);

		// The read of the old version finishes, but the file changes before it is taken.
		jobs.RunAll();
		Rewrite(path, Filled(32, 'B'));
		file.CheckDisk();
		CHECK(jobs.Count() == 1);

		file.Update();
		CHECK_FALSE(file.ByteAt(0).has_value());
		CHECK(file.IsWaiting());

		Land(file, jobs);
		CHECK(file.ByteAt(0) == 'B');
		CHECK_FALSE(file.IsWaiting());
	}

	SUBCASE("A failure")
	{
		std::FILE* held = ::_wfsopen(path.c_str(), L"rb", _SH_DENYRW);
		REQUIRE(held != nullptr);

		HexFile file(path, jobs.Submitter());
		file.Want(0, 31);
		jobs.RunAll();
		std::fclose(held);

		Rewrite(path, Filled(32, 'B'));
		file.CheckDisk();
		file.Update();
		CHECK(file.GetFailure().empty());
		CHECK(file.IsWaiting());

		Land(file, jobs);
		CHECK(file.GetFailure().empty());
		CHECK(file.ByteAt(0) == 'B');
	}
}
