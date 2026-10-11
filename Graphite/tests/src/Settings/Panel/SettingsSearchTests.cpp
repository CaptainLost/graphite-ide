#include <doctest/doctest.h>

#include "FakeSettingsPage.h"

#include "Settings/Panel/SettingsSearch.h"

#include "Core/Base/Base.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

	Graphite::SettingsRow Row(std::string title, std::string description, std::vector<std::string> keywords = {})
	{
		return Graphite::SettingsRow{.Title = std::move(title), .Description = std::move(description), .Keywords = std::move(keywords)};
	}

	std::string CommandsMatching(std::string_view query)
	{
		return query == "wrap" ? "1 command matches" : "";
	}

	Graphite::SettingsDirectory SampleDirectory()
	{
		Graphite::SettingsCard text{.Heading = "Text"};
		text.Rows.push_back(Row("Font size", "Size of code text.", {"zoom"}));
		text.Rows.push_back(Row("Word wrap", "Long lines continue on the next line."));

		Graphite::SettingsCard markers{.Heading = "Git changes"};
		markers.Rows.push_back(Row("Markers in the gutter", "Coloured bars beside changed lines."));

		Graphite::SettingsCard diffs{.Heading = "Diffs"};
		diffs.Rows.push_back(Row("Wrap long lines", "The diff toolbar has the same switch."));

		Graphite::SettingsCard shortcuts{.Heading = "Shortcuts"};
		shortcuts.Rows.push_back(Graphite::SettingsRow{.Title = "Shortcuts",
													   .Keywords = {"shortcut", "hotkey"},
													   .DrawBlock = [] {},
													   .DescribeMatches = CommandsMatching});

		Graphite::SettingsCard server{.Heading = "Server"};
		server.Rows.push_back(Row("Language server", "Roslyn is recommended."));

		Graphite::SettingsGroup languages = GroupOf("");
		languages.Pages.push_back(Core::CreateScope<FakeSettingsPage>("CSharp", "C#", std::vector<Graphite::SettingsCard>{server}));
		std::vector<Graphite::SettingsGroup> childGroups;
		childGroups.push_back(std::move(languages));

		Core::Scope<FakeSettingsPage> servers = Core::CreateScope<FakeSettingsPage>("languageServers", "Language Servers");
		servers->SetChildren(Graphite::SettingsDirectory{std::move(childGroups)});

		Graphite::SettingsGroup general = GroupOf("General");
		general.Pages.push_back(Core::CreateScope<FakeSettingsPage>("keyBindings", "Key Bindings", std::vector<Graphite::SettingsCard>{shortcuts}));
		general.Pages.push_back(std::move(servers));

		Graphite::SettingsGroup panels = GroupOf("Panels");
		panels.Pages.push_back(Core::CreateScope<FakeSettingsPage>("editor", "Editor", std::vector<Graphite::SettingsCard>{text, markers}));
		panels.Pages.push_back(Core::CreateScope<FakeSettingsPage>("git", "Git", std::vector<Graphite::SettingsCard>{diffs}));

		std::vector<Graphite::SettingsGroup> groups;
		groups.push_back(std::move(general));
		groups.push_back(std::move(panels));

		return Graphite::SettingsDirectory{std::move(groups)};
	}

	std::vector<std::string> TitlesOf(const std::vector<Graphite::SettingsPageHits>& hits)
	{
		std::vector<std::string> titles;

		for (const Graphite::SettingsPageHits& page : hits)
		{
			for (const Graphite::SettingsCardHits& card : page.Cards)
			{
				for (const Graphite::SettingsRowHit& row : card.Rows)
				{
					titles.push_back(row.Row.Title);
				}
			}
		}

		return titles;
	}
}

TEST_CASE("A query's words are lowered and empty ones dropped")
{
	CHECK(Graphite::SplitQueryWords("  Wrap   LONG ") == std::vector<std::string>{"wrap", "long"});
	CHECK(Graphite::SplitQueryWords("   ").empty());
}

TEST_CASE("A query of spaces only is not a search")
{
	CHECK_FALSE(Graphite::IsSearchQuery(""));
	CHECK_FALSE(Graphite::IsSearchQuery("   "));
	CHECK(Graphite::IsSearchQuery(" wrap "));
}

TEST_CASE("A row matches when every word of the query is found in it")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(TitlesOf(Graphite::FindSettings(directory, "wrap next")) == std::vector<std::string>{"Word wrap"});
	CHECK(Graphite::FindSettings(directory, "wrap tabs").empty());
}

TEST_CASE("Case does not matter to the search")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(TitlesOf(Graphite::FindSettings(directory, "FONT")) == std::vector<std::string>{"Font size"});
}

TEST_CASE("A row is found through its page title, its card heading and its keywords")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(TitlesOf(Graphite::FindSettings(directory, "git")) == std::vector<std::string>{"Markers in the gutter", "Wrap long lines"});
	CHECK(TitlesOf(Graphite::FindSettings(directory, "diffs")) == std::vector<std::string>{"Wrap long lines"});
	CHECK(TitlesOf(Graphite::FindSettings(directory, "zoom")) == std::vector<std::string>{"Font size"});
}

TEST_CASE("Hits come in list order, page by page and card by card")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	const std::vector<Graphite::SettingsPageHits> hits = Graphite::FindSettings(directory, "wrap");

	REQUIRE(hits.size() == 3);
	CHECK(hits[0].Path == std::vector<std::string>{"keyBindings"});
	CHECK(hits[1].Path == std::vector<std::string>{"editor"});
	CHECK(hits[2].Path == std::vector<std::string>{"git"});
	CHECK(hits[1].Label == "Editor");
	CHECK(hits[1].Cards.size() == 1);
	CHECK(hits[1].Cards[0].Heading == "Text");
}

TEST_CASE("A page inside a page carries its path and a label naming its parent")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	const std::vector<Graphite::SettingsPageHits> hits = Graphite::FindSettings(directory, "roslyn");

	REQUIRE(hits.size() == 1);
	CHECK(hits[0].Path == std::vector<std::string>{"languageServers", "CSharp"});
	CHECK(hits[0].Label == "Language Servers \xE2\x80\xBA C#");
}

TEST_CASE("A page inside a page is found by its parent's title")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(TitlesOf(Graphite::FindSettings(directory, "language servers")) == std::vector<std::string>{"Language server"});
}

TEST_CASE("A block is found by what it finds inside before its title")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	const std::vector<Graphite::SettingsPageHits> byCommand = Graphite::FindSettings(directory, "wrap");
	REQUIRE_FALSE(byCommand.empty());
	CHECK(byCommand[0].Cards[0].Rows[0].MatchesInside == "1 command matches");

	const std::vector<Graphite::SettingsPageHits> byKeyword = Graphite::FindSettings(directory, "hotkey");
	REQUIRE(byKeyword.size() == 1);
	CHECK(byKeyword[0].Cards[0].Rows[0].Row.Title == "Shortcuts");
	CHECK(byKeyword[0].Cards[0].Rows[0].MatchesInside.empty());
}

TEST_CASE("An empty query or one of spaces finds nothing")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(Graphite::FindSettings(directory, "").empty());
	CHECK(Graphite::FindSettings(directory, "   ").empty());
}

TEST_CASE("A query outside ASCII finds nothing rather than failing")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	CHECK(Graphite::FindSettings(directory, "za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87").empty());
}

TEST_CASE("HasHitsUnder names the pages of the list that have hits")
{
	const Graphite::SettingsDirectory directory = SampleDirectory();

	const std::vector<Graphite::SettingsPageHits> hits = Graphite::FindSettings(directory, "roslyn");

	CHECK(Graphite::HasHitsUnder(hits, "languageServers"));
	CHECK_FALSE(Graphite::HasHitsUnder(hits, "editor"));
}
