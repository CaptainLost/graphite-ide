#include <doctest/doctest.h>

#include "Core/Text/SharedFileReader.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>

using namespace Core;

namespace {
	constexpr std::size_t kChunkBytes = 4;

	std::string ReadEverything(SharedFileReader& reader)
	{
		std::string everything;
		std::array<char, kChunkBytes> chunk{};

		while (true)
		{
			const Result<std::size_t> bytesRead = reader.ReadInto(chunk);
			REQUIRE(bytesRead.HasValue());

			if (bytesRead.GetValue() == 0)
			{
				return everything;
			}

			everything.append(chunk.data(), bytesRead.GetValue());
		}
	}
}

TEST_CASE("A file being read can be renamed and deleted, and the read goes on to the end")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.log";
	const std::filesystem::path renamed = directory.GetPath() / "renamed.log";
	WriteFileBytes(path, "first line\nsecond line\n");

	Result<SharedFileReader> opened = SharedFileReader::Open(path);
	REQUIRE(opened.HasValue());
	SharedFileReader reader = std::move(opened).GetValue();

	std::error_code renameError;
	std::filesystem::rename(path, renamed, renameError);
	CHECK_FALSE(renameError);

	std::error_code removeError;
	std::filesystem::remove(renamed, removeError);
	CHECK_FALSE(removeError);

	CHECK(ReadEverything(reader) == "first line\nsecond line\n");
}

TEST_CASE("A file that is not there cannot be opened for reading and says it is gone")
{
	TempDirectory directory;

	const Result<SharedFileReader> opened = SharedFileReader::Open(directory.GetPath() / "not-there.log");

	REQUIRE_FALSE(opened.HasValue());
	CHECK(opened.GetError().Code == ErrorCode::NotFound);
}
