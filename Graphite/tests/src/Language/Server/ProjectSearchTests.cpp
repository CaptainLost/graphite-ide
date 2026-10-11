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

	const Graphite::ProjectsBelow found =
		Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", Graphite::kMostProjectsLoaded, Core::CancellationToken{});

	CHECK(found.Kept == std::vector<std::filesystem::path>{temp.GetPath() / "App" / "App.csproj",
														   temp.GetPath() / "src" / "Library" / "Library.CSPROJ"});
	CHECK(found.DroppedAboveLimit == 0);
}

TEST_CASE("Above the limit projects in test folders are dropped first")
{
	TempDirectory temp;
	temp.MakeFile("A/A.csproj", "");
	temp.MakeFile("B/B.csproj", "");
	temp.MakeFile("Test/B.Tests/B.Tests.csproj", "");
	temp.MakeFile("tests/A.Tests/A.Tests.csproj", "");

	const Graphite::ProjectsBelow three = Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", 3, Core::CancellationToken{});
	const Graphite::ProjectsBelow one = Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", 1, Core::CancellationToken{});

	CHECK(three.Kept == std::vector<std::filesystem::path>{temp.GetPath() / "A" / "A.csproj", temp.GetPath() / "B" / "B.csproj",
														   temp.GetPath() / "Test" / "B.Tests" / "B.Tests.csproj"});
	CHECK(three.DroppedAboveLimit == 1);
	CHECK(one.Kept == std::vector<std::filesystem::path>{temp.GetPath() / "A" / "A.csproj"});
	CHECK(one.DroppedAboveLimit == 3);
}

TEST_CASE("A cancelled search finds nothing")
{
	TempDirectory temp;
	temp.MakeFile("App/App.csproj", "");
	Core::CancellationToken token;
	token.Cancel();

	CHECK(Graphite::FindProjectsBelow(temp.GetPath(), ".csproj", Graphite::kMostProjectsLoaded, token).Kept.empty());
}
