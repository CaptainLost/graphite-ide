#include <doctest/doctest.h>

#include "FakeSettingsPage.h"

#include "Settings/Panel/SettingsDirectory.h"

#include "Core/Base/Base.h"

#include <string>
#include <utility>
#include <vector>

namespace {

	std::vector<std::string> IdsOf(const std::vector<Graphite::SettingsPage*>& pages)
	{
		std::vector<std::string> ids;

		for (const Graphite::SettingsPage* page : pages)
		{
			ids.emplace_back(page->GetId());
		}

		return ids;
	}

	Graphite::SettingsDirectory ServersWithTwoLanguages()
	{
		Graphite::SettingsGroup languages = GroupOf("");
		languages.Pages.push_back(Core::CreateScope<FakeSettingsPage>("Cpp", "C++"));
		languages.Pages.push_back(Core::CreateScope<FakeSettingsPage>("CSharp", "C#"));

		std::vector<Graphite::SettingsGroup> childGroups;
		childGroups.push_back(std::move(languages));

		Core::Scope<FakeSettingsPage> servers = Core::CreateScope<FakeSettingsPage>("languageServers", "Language Servers");
		servers->SetChildren(Graphite::SettingsDirectory{std::move(childGroups)});

		Graphite::SettingsGroup general = GroupOf("General");
		general.Pages.push_back(Core::CreateScope<FakeSettingsPage>("appearance", "Appearance"));
		general.Pages.push_back(std::move(servers));

		Graphite::SettingsGroup panels = GroupOf("Panels");
		panels.Pages.push_back(Core::CreateScope<FakeSettingsPage>("editor", "Editor"));

		std::vector<Graphite::SettingsGroup> groups;
		groups.push_back(std::move(general));
		groups.push_back(std::move(panels));

		return Graphite::SettingsDirectory{std::move(groups)};
	}
}

TEST_CASE("A directory finds a page by its id in any group")
{
	const Graphite::SettingsDirectory directory = ServersWithTwoLanguages();

	REQUIRE(directory.Find("editor") != nullptr);
	CHECK(directory.Find("editor")->GetTitle() == "Editor");
	CHECK(directory.Find("missing") == nullptr);
}

TEST_CASE("A directory does not find a page that sits inside another page")
{
	const Graphite::SettingsDirectory directory = ServersWithTwoLanguages();

	CHECK(directory.Find("CSharp") == nullptr);
}

TEST_CASE("A directory's first page is the first page of its first group with any")
{
	std::vector<Graphite::SettingsGroup> groups;
	groups.push_back(GroupOf("Empty"));
	groups.push_back(GroupOf("Panels"));
	groups.back().Pages.push_back(Core::CreateScope<FakeSettingsPage>("editor", "Editor"));

	const Graphite::SettingsDirectory directory{std::move(groups)};

	REQUIRE(directory.First() != nullptr);
	CHECK(directory.First()->GetId() == "editor");
	CHECK_FALSE(directory.IsEmpty());
	CHECK(Graphite::SettingsDirectory{}.IsEmpty());
}

TEST_CASE("Every page is collected in list order, a page's own pages straight after it")
{
	const Graphite::SettingsDirectory directory = ServersWithTwoLanguages();

	CHECK(IdsOf(Graphite::CollectPages(directory)) ==
		  std::vector<std::string>{"appearance", "languageServers", "Cpp", "CSharp", "editor"});
}
