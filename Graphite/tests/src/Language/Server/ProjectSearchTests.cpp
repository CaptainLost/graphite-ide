#include <doctest/doctest.h>

#include "Language/Server/ProjectSearch.h"
#include "TempDirectory.h"

#include "Core/Jobs/CancellationToken.h"

#include <filesystem>
#include <ostream>
#include <vector>

TEST_CASE("Projects are found below the root and not in build output")
{
	TempDirectory temp;
	temp.MakeFile("App/App.csproj", "");
	temp.MakeFile("src/Library/Library.CSPROJ", "");
	temp.MakeFile("App/bin/Debug/Copy.csproj", "");
	temp.MakeFile("App/Obj/Generated.csproj", "");
	temp.MakeFile("web/node_modules/package/Package.csproj", "");
	temp.MakeFile(".vs/Cache.csproj", "");
	temp.MakeFile(".hidden/Hidden.csproj", "");
	temp.MakeFile("App/Notes.txt", "");

	const std::vector<std::filesystem::path> found =
		Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", Graphite::kMostProjectsLoaded, Core::CancellationToken{});

	CHECK(found == std::vector<std::filesystem::path>{temp.GetPath() / "App" / "App.csproj",
													  temp.GetPath() / "src" / "Library" / "Library.CSPROJ"});
}

TEST_CASE("Above the limit projects in test folders are dropped first")
{
	TempDirectory temp;
	temp.MakeFile("A/A.csproj", "");
	temp.MakeFile("B/B.csproj", "");
	temp.MakeFile("Test/B.Tests/B.Tests.csproj", "");
	temp.MakeFile("tests/A.Tests/A.Tests.csproj", "");

	const std::vector<std::filesystem::path> three = Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", 3, Core::CancellationToken{});
	const std::vector<std::filesystem::path> one = Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", 1, Core::CancellationToken{});

	CHECK(three == std::vector<std::filesystem::path>{temp.GetPath() / "A" / "A.csproj", temp.GetPath() / "B" / "B.csproj",
													  temp.GetPath() / "Test" / "B.Tests" / "B.Tests.csproj"});
	CHECK(one == std::vector<std::filesystem::path>{temp.GetPath() / "A" / "A.csproj"});
}

TEST_CASE("A cancelled search finds nothing")
{
	TempDirectory temp;
	temp.MakeFile("App/App.csproj", "");
	Core::CancellationToken token;
	token.Cancel();

	CHECK(Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", Graphite::kMostProjectsLoaded, token).empty());
}
