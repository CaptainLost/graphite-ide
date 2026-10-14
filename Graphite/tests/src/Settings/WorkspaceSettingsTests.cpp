#include <doctest/doctest.h>

#include "Settings/WorkspaceSettings.h"

#include "Core/Base/JsonFile.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include <filesystem>
#include <string>

TEST_CASE("WorkspaceSettings keeps what was written and reads it back")
{
	TempDirectory temp;

	Graphite::WorkspaceSettings settings{temp.GetPath()};
	settings.Write("a", "1");
	settings.Write("b", "2");

	CHECK(settings.Read("a") == "1");
	CHECK(Graphite::WorkspaceSettings{temp.GetPath()}.Read("b") == "2");
}

TEST_CASE("WorkspaceSettings keeps a copy of a corrupt file before a write goes over it")
{
	TempDirectory temp;
	const std::string corrupt = R"({"a": "1", oops)";
	const std::filesystem::path file = temp.MakeFile(".graphite/workspace.json", corrupt);

	Graphite::WorkspaceSettings settings{temp.GetPath()};
	settings.Write("b", "2");

	const std::filesystem::path backup = temp.GetPath() / ".graphite" / "workspace.json.bak";

	REQUIRE(std::filesystem::exists(backup));
	CHECK(ReadWholeFile(backup) == corrupt);
	CHECK(Core::JsonFile::Load(file).Get("b") == "2");
}
