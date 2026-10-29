#include <doctest/doctest.h>

#include "Core/FileSystem/FileRange.h"

#include "FileFixtures.h"
#include "SparseFileFixture.h"
#include "TempDirectory.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef CORE_PLATFORM_WINDOWS
#include <share.h>
#include <stdio.h>
#endif

using Core::ReadFileRange;

namespace {
	std::string AsText(const std::vector<std::uint8_t>& bytes)
	{
		return std::string{bytes.begin(), bytes.end()};
	}
}

TEST_CASE("A byte range is read from its offset")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "digits.bin";
	WriteFileBytes(path, "0123456789");

	const auto range = ReadFileRange(path, 3, 4);

	REQUIRE(range);
	CHECK(AsText(range.GetValue()) == "3456");
}

TEST_CASE("A range past the end comes back short or empty")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "digits.bin";
	WriteFileBytes(path, "0123456789");

	const auto shortRange = ReadFileRange(path, 8, 10);
	const auto emptyRange = ReadFileRange(path, 10, 4);
	const auto farRange = ReadFileRange(path, 1000, 4);

	REQUIRE(shortRange);
	CHECK(AsText(shortRange.GetValue()) == "89");
	REQUIRE(emptyRange);
	CHECK(emptyRange.GetValue().empty());
	REQUIRE(farRange);
	CHECK(farRange.GetValue().empty());
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("A range far into a sparse file reads zeros")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "huge.bin";
	WriteFileBytes(path, "");

	if (!MarkFileSparse(path))
	{
		return;
	}

	constexpr std::uint64_t kFileSize = std::uint64_t{5} * 1024 * 1024 * 1024;
	std::error_code resizeError;
	std::filesystem::resize_file(path, kFileSize, resizeError);
	REQUIRE_FALSE(resizeError);

	const auto range = ReadFileRange(path, kFileSize - 16, 64);

	REQUIRE(range);
	CHECK(range.GetValue() == std::vector<std::uint8_t>(16, 0));
}
#else
TEST_CASE("A range far into a sparse file reads zeros" * doctest::skip())
{
}
#endif

TEST_CASE("A missing file is NotFound")
{
	TempDirectory directory;

	const auto range = ReadFileRange(directory.GetPath() / "missing.bin", 0, 4);

	REQUIRE_FALSE(range);
	CHECK(range.GetError().Code == Core::ErrorCode::NotFound);
}

#ifdef CORE_PLATFORM_WINDOWS
TEST_CASE("A file held with no sharing is AccessDenied")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "held.bin";
	WriteFileBytes(path, "0123456789");

	FILE* holder = ::_wfsopen(path.c_str(), L"rb", _SH_DENYRW);
	REQUIRE(holder != nullptr);

	const auto range = ReadFileRange(path, 0, 4);

	::fclose(holder);

	REQUIRE_FALSE(range);
	CHECK(range.GetError().Code == Core::ErrorCode::AccessDenied);
}

TEST_CASE("A read leaves no handle open, so the file can be renamed and deleted after it")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "free.bin";
	const std::filesystem::path renamed = directory.GetPath() / "renamed.bin";
	WriteFileBytes(path, "0123456789");

	REQUIRE(ReadFileRange(path, 0, 4));

	std::error_code renameError;
	std::filesystem::rename(path, renamed, renameError);
	CHECK_FALSE(renameError);

	std::error_code removeError;
	CHECK(std::filesystem::remove(renamed, removeError));
	CHECK_FALSE(removeError);
}

TEST_CASE("Reading works while another handle writes the file")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "written.bin";
	WriteFileBytes(path, "0123456789");

	FILE* writer = ::_wfsopen(path.c_str(), L"r+b", _SH_DENYNO);
	REQUIRE(writer != nullptr);

	const auto range = ReadFileRange(path, 2, 3);

	::fclose(writer);

	REQUIRE(range);
	CHECK(AsText(range.GetValue()) == "234");
}
#endif
