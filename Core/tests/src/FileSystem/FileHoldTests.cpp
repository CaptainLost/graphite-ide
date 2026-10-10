#include <doctest/doctest.h>

#include "Core/FileSystem/FileHold.h"
#include "Core/FileSystem/FileSystem.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace {

	std::filesystem::path FileIn(TempDirectory& temporaryDirectory)
	{
		const std::filesystem::path folder = temporaryDirectory.GetPath() / Core::FromUtf8("wersja Ω");
		std::filesystem::create_directories(folder);

		const std::filesystem::path file = folder / "Server.dll";
		std::ofstream{file} << "an assembly";
		return file;
	}
}

TEST_CASE("A held file can be read but not deleted and its directory cannot be renamed")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = FileIn(temporaryDirectory);

	const Core::Result<Core::FileHold> hold = Core::FileHold::HoldOpen(file);
	REQUIRE(hold);

	std::string contents;
	std::ifstream{file} >> contents;
	CHECK(contents == "an");

	std::error_code removeError;
	CHECK_FALSE(std::filesystem::remove(file, removeError));

	std::error_code renameError;
	std::filesystem::rename(file.parent_path(), temporaryDirectory.GetPath() / "aside", renameError);
	CHECK(renameError);
}

TEST_CASE("A file let go of can be deleted")
{
	TempDirectory temporaryDirectory;
	const std::filesystem::path file = FileIn(temporaryDirectory);

	{
		Core::Result<Core::FileHold> hold = Core::FileHold::HoldOpen(file);
		REQUIRE(hold);

		Core::FileHold moved = std::move(hold.GetValue());
	}

	std::error_code removeError;
	CHECK(std::filesystem::remove(file, removeError));
}

TEST_CASE("Holding a file that does not exist fails")
{
	TempDirectory temporaryDirectory;

	CHECK_FALSE(Core::FileHold::HoldOpen(temporaryDirectory.GetPath() / "absent.dll"));
}
