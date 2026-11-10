#include <doctest/doctest.h>

#include "Files/FileRequests.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using nlohmann::json;

namespace {

	std::string Read(const std::filesystem::path& file)
	{
		std::ifstream stream{file, std::ios::binary};
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	std::string PathText(const std::filesystem::path& path)
	{
		return path.string(); // not a path conversion: Linux paths are their bytes
	}
}

TEST_CASE("writeFile writes the payload to a new file")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "a b.txt";

	const auto written = Agent::WriteFile(json{{"path", PathText(file)}}, "hello\n");

	REQUIRE(written.HasValue());
	CHECK(Read(file) == "hello\n");
}

TEST_CASE("writeFile over an executable file keeps it executable")
{
	TempDirectory directory;
	const std::filesystem::path script = directory.MakeFile("run.sh", "#!/bin/sh\n");
	std::filesystem::permissions(script, std::filesystem::perms::owner_all);

	REQUIRE(Agent::WriteFile(json{{"path", PathText(script)}}, "#!/bin/sh\necho hi\n").HasValue());

	CHECK(std::filesystem::status(script).permissions() == std::filesystem::perms::owner_all);
	CHECK(Read(script) == "#!/bin/sh\necho hi\n");
}

TEST_CASE("writeFile asked for an executable leaves the file runnable")
{
	TempDirectory directory;
	const std::filesystem::path script = directory.GetPath() / "graphite";

	REQUIRE(Agent::WriteFile(json{{"path", PathText(script)}, {"executable", true}}, "#!/bin/sh\n").HasValue());

	const auto permissions = std::filesystem::status(script).permissions();
	CHECK((permissions & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
	CHECK((permissions & std::filesystem::perms::others_exec) != std::filesystem::perms::none);
}

TEST_CASE("rename onto a name that is taken fails and leaves both files")
{
	TempDirectory directory;
	const std::filesystem::path from = directory.MakeFile("from.txt", "from");
	const std::filesystem::path to = directory.MakeFile("to.txt", "to");

	const auto renamed = Agent::Rename(json{{"from", PathText(from)}, {"to", PathText(to)}});

	CHECK_FALSE(renamed.HasValue());
	CHECK(Read(from) == "from");
	CHECK(Read(to) == "to");
}

TEST_CASE("rename moves a file to a free name")
{
	TempDirectory directory;
	const std::filesystem::path from = directory.MakeFile("from.txt", "x");
	const std::filesystem::path to = directory.GetPath() / "to.txt";

	REQUIRE(Agent::Rename(json{{"from", PathText(from)}, {"to", PathText(to)}}).HasValue());

	CHECK_FALSE(std::filesystem::exists(from));
	CHECK(Read(to) == "x");
}

TEST_CASE("createFile makes an empty file once and refuses the second time")
{
	TempDirectory directory;
	const std::filesystem::path file = directory.GetPath() / "new.txt";

	REQUIRE(Agent::CreateFile(json{{"path", PathText(file)}}).HasValue());
	CHECK(std::filesystem::file_size(file) == 0);

	const auto again = Agent::CreateFile(json{{"path", PathText(file)}});
	REQUIRE_FALSE(again.HasValue());
	CHECK(again.GetError().Code == Core::ErrorCode::InvalidArgument);
}

TEST_CASE("makeDirectory makes the folders on the way")
{
	TempDirectory directory;
	const std::filesystem::path nested = directory.GetPath() / "a" / "b" / "c";

	REQUIRE(Agent::MakeDirectory(json{{"path", PathText(nested)}}).HasValue());

	CHECK(std::filesystem::is_directory(nested));
}

TEST_CASE("A request without its path is an invalid argument")
{
	const auto written = Agent::WriteFile(json::object(), "x");

	REQUIRE_FALSE(written.HasValue());
	CHECK(written.GetError().Code == Core::ErrorCode::InvalidArgument);
}
