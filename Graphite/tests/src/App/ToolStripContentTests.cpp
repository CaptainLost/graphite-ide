#include <doctest/doctest.h>

#include "App/ToolStripContent.h"

#include <IconsLucide.h>

#include <string>
#include <vector>

namespace {
	using Graphite::ToolStripInputs;
	using Graphite::ToolStripPanel;
	using Groups = std::vector<std::vector<std::string>>;

	ToolStripPanel PanelOf(const char* id, const char* group)
	{
		ToolStripPanel panel;
		panel.Id = id;
		panel.Title = id;
		panel.MenuGroup = group;
		return panel;
	}

	ToolStripInputs StandardInputs()
	{
		ToolStripInputs inputs;
		inputs.Panels = {
			PanelOf("files", "navigate"),
			PanelOf("console", "tools"),
			PanelOf("search", "navigate"),
			PanelOf("references", "navigate"),
			PanelOf("problems", "tools"),
			PanelOf("git", "git"),
			PanelOf("changes", "git"),
			PanelOf("sessions", "ai"),
			PanelOf("settings", "window"),
		};
		inputs.Colours = {1, 2, 3, 4};
		return inputs;
	}

	Groups IdsOf(const std::vector<std::vector<Core::ToolStripItem>>& groups)
	{
		Groups ids;
		for (const auto& group : groups)
		{
			ids.emplace_back();
			for (const Core::ToolStripItem& item : group)
			{
				ids.back().push_back(item.Id);
			}
		}
		return ids;
	}

	Core::ToolStripItem Find(const Core::ToolStripContent& content, const std::string& id)
	{
		for (const auto* groups : {&content.Top, &content.Bottom})
		{
			for (const auto& group : *groups)
			{
				for (const Core::ToolStripItem& item : group)
				{
					if (item.Id == id)
					{
						return item;
					}
				}
			}
		}
		FAIL("no item with id " << id);
		return {};
	}

	Core::ToolStripItem ItemOf(const ToolStripInputs& inputs, const std::string& id)
	{
		return Find(Graphite::BuildToolStripContent(inputs), id);
	}
}

TEST_CASE("Top groups run navigate, git, ai and keep registration order inside each")
{
	const Core::ToolStripContent content = Graphite::BuildToolStripContent(StandardInputs());
	CHECK(IdsOf(content.Top) == Groups{{"files", "search", "references"}, {"git", "changes"}, {"sessions"}});
}

TEST_CASE("Bottom groups are tools with the terminals first, then window")
{
	const Core::ToolStripContent content = Graphite::BuildToolStripContent(StandardInputs());
	CHECK(IdsOf(content.Bottom) == Groups{{"terminals", "console", "problems"}, {"settings"}});
}

TEST_CASE("A panel in a group the strip does not know lands at the end of the top block")
{
	ToolStripInputs inputs = StandardInputs();
	inputs.Panels.push_back(PanelOf("build", "build/run"));
	const Core::ToolStripContent content = Graphite::BuildToolStripContent(inputs);
	REQUIRE(content.Top.size() == 4);
	CHECK(IdsOf(content.Top).back() == std::vector<std::string>{"build"});
}

TEST_CASE("A group with no panels draws no empty group")
{
	ToolStripInputs inputs = StandardInputs();
	inputs.Panels.erase(inputs.Panels.begin() + 7);
	const Core::ToolStripContent content = Graphite::BuildToolStripContent(inputs);
	CHECK(content.Top.size() == 2);
}

TEST_CASE("An item is open when its panel is visible")
{
	ToolStripInputs inputs = StandardInputs();
	inputs.Panels[0].Visible = true;
	CHECK(ItemOf(inputs, "files").Open);
	CHECK_FALSE(ItemOf(inputs, "search").Open);
}

TEST_CASE("The terminals' item reads their look")
{
	ToolStripInputs inputs = StandardInputs();
	inputs.Terminals = {true, false};
	const Core::ToolStripItem open = ItemOf(inputs, "terminals");
	CHECK(open.Open);
	CHECK_FALSE(open.Badge.has_value());

	inputs.Terminals = {false, true};
	const Core::ToolStripItem running = ItemOf(inputs, "terminals");
	CHECK_FALSE(running.Open);
	REQUIRE(running.Badge.has_value());
	CHECK(*running.Badge == inputs.Colours.Running);
}

TEST_CASE("The tooltip is the title, with the shortcut in brackets when one is bound")
{
	ToolStripInputs inputs = StandardInputs();
	inputs.Panels[0].Title = "Explorer";
	CHECK(ItemOf(inputs, "files").Tooltip == "Explorer");
	inputs.Panels[0].Shortcut = "Alt+1";
	CHECK(ItemOf(inputs, "files").Tooltip == "Explorer (Alt+1)");
	CHECK(ItemOf(inputs, "terminals").Tooltip == "Terminal");
}

TEST_CASE("Changes carries a badge while the tree has changes")
{
	ToolStripInputs inputs = StandardInputs();
	CHECK_FALSE(ItemOf(inputs, "changes").Badge.has_value());
	inputs.HasChanges = true;
	const Core::ToolStripItem changes = ItemOf(inputs, "changes");
	REQUIRE(changes.Badge.has_value());
	CHECK(*changes.Badge == inputs.Colours.Changes);
}

TEST_CASE("Problems carries the error colour while there are errors, else the warning colour while there are warnings, else none")
{
	ToolStripInputs inputs = StandardInputs();
	CHECK_FALSE(ItemOf(inputs, "problems").Badge.has_value());

	inputs.Warnings = 2;
	REQUIRE(ItemOf(inputs, "problems").Badge.has_value());
	CHECK(*ItemOf(inputs, "problems").Badge == inputs.Colours.Warning);

	inputs.Errors = 1;
	CHECK(*ItemOf(inputs, "problems").Badge == inputs.Colours.Error);
}

TEST_CASE("The terminals' item uses the console icon")
{
	CHECK(ItemOf(StandardInputs(), "terminals").Icon == ICON_LC_SQUARE_TERMINAL);
}
