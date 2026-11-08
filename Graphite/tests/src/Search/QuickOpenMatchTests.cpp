#include <doctest/doctest.h>

#include "Search/QuickOpenMatch.h"

#include <string>
#include <vector>

using Graphite::RankQuickOpenMatches;
using Graphite::ScoreQuickOpenMatch;

TEST_CASE("Quick Open matches the typed characters in order, case and spaces ignored")
{
	CHECK(ScoreQuickOpenMatch("wsc", "Graphite/src/Settings/WorkspaceSettings.cpp").has_value());
	CHECK(ScoreQuickOpenMatch("WORK set", "Graphite/src/Settings/WorkspaceSettings.cpp").has_value());
	CHECK_FALSE(ScoreQuickOpenMatch("zzz", "WorkspaceSettings.cpp").has_value());
}

TEST_CASE("Quick Open puts a match in the file's own name before one spread over its folders")
{
	const std::vector<std::string> paths = {"Graphite/src/Workspace/Settings/Container.cpp", "Graphite/src/Settings/WorkspaceSettings.cpp"};

	const std::vector<std::size_t> ranked = RankQuickOpenMatches("workspacesettings", paths, 10);

	REQUIRE(ranked.size() == 2);
	CHECK(ranked.front() == 1);
}

TEST_CASE("Quick Open prefers the start of a name and of its words")
{
	const std::vector<std::string> paths = {"src/a/ImageTabs.cpp", "src/b/StatusBar.cpp"};

	CHECK(RankQuickOpenMatches("sb", paths, 10).front() == 1);
}

TEST_CASE("Quick Open with nothing typed keeps the order given, cut to the limit")
{
	const std::vector<std::string> paths = {"c.txt", "a.txt", "b.txt"};

	CHECK(RankQuickOpenMatches("", paths, 2) == std::vector<std::size_t>{0, 1});
	CHECK(RankQuickOpenMatches("xyz", paths, 10).empty());
}
