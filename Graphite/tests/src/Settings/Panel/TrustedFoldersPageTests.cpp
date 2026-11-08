#include <doctest/doctest.h>

#include "SettingsPageFrames.h"
#include "TempDirectory.h"

#include "Settings/Panel/TrustedFoldersPage.h"
#include "Workspace/WorkspaceTrust.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

	struct TrustScreen
	{
		TempDirectory Temp;
		Graphite::WorkspaceTrust Trust{Temp.GetPath() / "trust.json"};
		Graphite::TrustedFoldersPage Page{Trust};
		SettingsPageFrames Frames{Page};
	};
}

TEST_CASE("With no folder trusted the page says so")
{
	TrustScreen screen;

	CHECK(screen.Frames.CardHeadings() == std::vector<std::string>{"Trusted folders"});
	CHECK(screen.Frames.RowTitles() == std::vector<std::string>{"No folder is trusted yet."});
}

TEST_CASE("Each trusted folder has a row, and Remove stops trusting it")
{
	TrustScreen screen;
	const std::filesystem::path project = screen.Temp.GetPath() / "project";
	std::filesystem::create_directories(project);
	screen.Trust.Trust(project);

	const std::vector<std::string> titles = screen.Frames.RowTitles();
	REQUIRE(titles.size() == 2);
	CHECK(titles.back() == "Trust another folder");

	screen.Frames.ClickControl(titles.front());

	CHECK(screen.Trust.GetEntries().empty());
}

TEST_CASE("The trusted folders card explains what trust allows")
{
	TrustScreen screen;

	const std::vector<Graphite::SettingsCard> cards = screen.Frames.DescribeInFrame();

	REQUIRE(cards.size() == 1);
	CHECK(cards.front().Note == "The language server and the background analysis run only in these folders and the folders inside them. "
								"Any other folder opens in restricted mode.");
}
