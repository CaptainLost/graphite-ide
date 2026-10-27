#include <doctest/doctest.h>

#include "DeferredJobs.h"
#include "TempDirectory.h"

#include "Hex/HexFile.h"
#include "Hex/HexFiles.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

	std::filesystem::path WriteFile(const std::filesystem::path& path, std::size_t size, char with)
	{
		const std::vector<char> bytes(size, with);
		std::ofstream stream{path, std::ios::binary | std::ios::trunc};
		stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return path;
	}

	// Moves the modified time on as well, so the change is seen even though the size stays the same.
	void Rewrite(const std::filesystem::path& path, std::size_t size, char with)
	{
		const std::filesystem::file_time_type before = std::filesystem::last_write_time(path);
		WriteFile(path, size, with);
		std::filesystem::last_write_time(path, before + std::chrono::seconds(60));
	}

	struct OpenFiles
	{
		TempDirectory Directory;
		DeferredJobs Jobs;
		std::filesystem::path First = WriteFile(Directory.GetPath() / std::filesystem::path(u8"pierwszy żółw.bin"), 32, 'A');
		std::filesystem::path Second = WriteFile(Directory.GetPath() / std::filesystem::path(u8"drugi źrebak.bin"), 32, 'B');
		Graphite::HexFiles Files{Jobs.Submitter()};

		void ReadBoth()
		{
			Files.For(First).Want(0, 31);
			Files.For(Second).Want(0, 31);
			Jobs.RunAll();
			Files.Update();
		}
	};
}

TEST_CASE("Hex files keep one model per open path and drop the closed ones")
{
	OpenFiles open;
	Graphite::HexFile& first = open.Files.For(open.First);
	CHECK(&open.Files.For(open.First) == &first);
	CHECK(&open.Files.For(open.Second) != &first);

	open.ReadBoth();
	CHECK(open.Files.For(open.First).ByteAt(0) == 'A');
	CHECK(open.Files.For(open.Second).ByteAt(0) == 'B');

	open.Files.Prune({open.Second});
	CHECK(open.Files.For(open.Second).ByteAt(0) == 'B');

	// Opened again, the closed file starts with nothing read.
	CHECK_FALSE(open.Files.For(open.First).ByteAt(0).has_value());

	SUBCASE("A read still running when its file is closed lands nowhere")
	{
		open.Files.For(open.First).Want(0, 31);
		REQUIRE(open.Jobs.Count() == 1);

		open.Files.Prune({});
		open.Jobs.RunAll();
		open.Files.Update();
		CHECK_FALSE(open.Files.For(open.First).ByteAt(0).has_value());
	}
}

TEST_CASE("Hex files pass a change on disk to the file it names")
{
	OpenFiles open;
	open.ReadBoth();

	Rewrite(open.First, 32, 'C');
	Rewrite(open.Second, 32, 'D');
	open.Files.CheckDisk(open.First);
	CHECK(open.Jobs.Count() == 1);

	// A path with no hex tab is not made into one.
	open.Files.CheckDisk(open.Directory.GetPath() / "other.bin");
	CHECK(open.Jobs.Count() == 1);

	open.Jobs.RunAll();
	open.Files.Update();
	CHECK(open.Files.For(open.First).ByteAt(0) == 'C');
	CHECK(open.Files.For(open.Second).ByteAt(0) == 'B');
}
