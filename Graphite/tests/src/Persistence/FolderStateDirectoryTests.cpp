#include <doctest/doctest.h>

#include "Persistence/FolderStateDirectory.h"

#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Repository.h"

#include <filesystem>
#include <string>

using namespace Graphite;

TEST_CASE("The folder's state directory is made with a .gitignore that ignores all of it")
{
	TempDirectory temp;

	REQUIRE(EnsureFolderStateDirectory(temp.GetPath()));

	const std::filesystem::path ignoreFile = temp.GetPath() / ".graphite" / ".gitignore";
	REQUIRE(std::filesystem::exists(ignoreFile));

	const Core::Result<std::string> contents = Core::ReadFileBytes(ignoreFile);
	REQUIRE(contents.HasValue());
	CHECK(contents.GetValue().find("\n*\n") != std::string::npos);
}

TEST_CASE("A .gitignore the reader changed in the state directory is left as it is")
{
	TempDirectory temp;
	temp.MakeFile(".graphite/.gitignore", "analysis/\n");

	REQUIRE(EnsureFolderStateDirectory(temp.GetPath()));

	const Core::Result<std::string> contents = Core::ReadFileBytes(temp.GetPath() / ".graphite" / ".gitignore");
	REQUIRE(contents.HasValue());
	CHECK(contents.GetValue() == "analysis/\n");
}

TEST_CASE("Git lists nothing of the folder's state directory")
{
	TempDirectory temp;
	REQUIRE(Core::Repository::Init(temp.GetPath()).HasValue());

	REQUIRE(EnsureFolderStateDirectory(temp.GetPath()));
	temp.MakeFile(".graphite/workspace.json", "{}");
	temp.MakeFile("source.txt", "mine");

	const Core::Result<Core::Repository> repository = Core::Repository::Open(temp.GetPath());
	REQUIRE(repository.HasValue());

	const Core::Result<Core::WorkingTreeStatus> status = repository.GetValue().QueryStatus();
	REQUIRE(status.HasValue());
	REQUIRE(status.GetValue().Entries.size() == 1);
	CHECK(status.GetValue().Entries.front().Path.filename() == "source.txt");
}
