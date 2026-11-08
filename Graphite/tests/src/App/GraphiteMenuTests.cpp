#include <doctest/doctest.h>

#include "App/GraphiteMenu.h"
#include "App/SampleToasts.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
	using Core::MenuItem;

	const MenuItem* FindSubmenu(const std::vector<MenuItem>& items, const std::string& label)
	{
		for (const MenuItem& item : items)
		{
			if (item.GetKind() == MenuItem::Kind::Submenu && item.GetText() == label)
			{
				return &item;
			}
		}

		return nullptr;
	}

	bool ListsCommand(const std::vector<MenuItem>& items, const std::string& commandId)
	{
		for (const MenuItem& item : items)
		{
			if (item.GetText() == commandId)
			{
				return true;
			}
		}

		return false;
	}

	std::size_t IndexOfGroup(const std::vector<MenuItem>& items, const std::string& groupName)
	{
		for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
		{
			if (items[itemIndex].GetKind() == MenuItem::Kind::Group && items[itemIndex].GetText() == groupName)
			{
				return itemIndex;
			}
		}

		return items.size();
	}

	std::vector<const MenuItem*> SectionBody(const std::vector<MenuItem>& items,
											 const std::string& heading)
	{
		std::vector<const MenuItem*> body;
		bool inside = false;

		for (const MenuItem& item : items)
		{
			if (item.GetKind() == MenuItem::Kind::Section)
			{
				if (inside)
				{
					break;
				}

				inside = item.GetText() == heading;
				continue;
			}

			if (inside)
			{
				body.push_back(&item);
			}
		}

		return body;
	}

	std::vector<std::string> SectionHeadings(const std::vector<MenuItem>& items)
	{
		std::vector<std::string> headings;

		for (const MenuItem& item : items)
		{
			if (item.GetKind() == MenuItem::Kind::Section)
			{
				headings.push_back(item.GetText());
			}
		}

		return headings;
	}
}

TEST_CASE("The panels submenu holds every panel, under a heading per role")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const MenuItem* panels = FindSubmenu(view->GetChildren(), "Panels");

	REQUIRE(panels != nullptr);
	CHECK(SectionHeadings(panels->GetChildren()) == std::vector<std::string>{"Navigate", "Tools", "Git", "AI"});

	for (const auto& [heading, group] : {std::pair{"Navigate", "panels.navigate"},
										 std::pair{"Tools", "panels.tools"},
										 std::pair{"Git", "panels.git"},
										 std::pair{"AI", "panels.ai"}})
	{
		const std::vector<const MenuItem*> members = SectionBody(panels->GetChildren(), heading);

		REQUIRE(members.size() == 1);
		CHECK(members[0]->GetKind() == MenuItem::Kind::Group);
		CHECK(members[0]->GetText() == group);
	}
}

TEST_CASE("The panels submenu opens the view menu, ahead of the settings it is looked at with")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);
	REQUIRE_FALSE(view->GetChildren().empty());

	const MenuItem& first = view->GetChildren().front();

	CHECK(first.GetKind() == MenuItem::Kind::Submenu);
	CHECK(first.GetText() == "Panels");
}

TEST_CASE("The editor's own view settings stay on the top level, where their shortcuts show")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	std::vector<std::string> commands;

	for (const MenuItem& item : view->GetChildren())
	{
		if (item.GetKind() == MenuItem::Kind::Command)
		{
			commands.push_back(item.GetText());
		}
	}

	CHECK(commands == std::vector<std::string>{"terminal.new", "editor.toggleWordWrap", "editor.zoomIn",
											   "editor.zoomOut", "editor.resetZoom",
											   "view.resetLayout"});
}

TEST_CASE("The explorer's ignored-file modes are reached through the panel that obeys them")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);
	CHECK(FindSubmenu(view->GetChildren(), "Git") == nullptr);

	const MenuItem* options = FindSubmenu(view->GetChildren(), "Explorer Options");

	REQUIRE(options != nullptr);

	const MenuItem* ignored = FindSubmenu(options->GetChildren(), "Git-Ignored Files");

	REQUIRE(ignored != nullptr);

	std::vector<std::string> modes;

	for (const MenuItem& item : ignored->GetChildren())
	{
		modes.push_back(item.GetText());
	}

	CHECK(modes == std::vector<std::string>{"git.ignored.show", "git.ignored.dim",
											"git.ignored.hide"});
}

TEST_CASE("Following the active file is switched among the explorer's options")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const MenuItem* options = FindSubmenu(view->GetChildren(), "Explorer Options");

	REQUIRE(options != nullptr);

	const bool listed = ListsCommand(options->GetChildren(), "explorer.followActiveFile");

	CHECK(listed);
}

TEST_CASE("The settings panel is reached from the top level, not from among the panels")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const MenuItem* panels = FindSubmenu(view->GetChildren(), "Panels");

	REQUIRE(panels != nullptr);

	for (const MenuItem& item : panels->GetChildren())
	{
		CHECK(item.GetText() != "panels.window");
	}

	const std::vector<MenuItem>& top = view->GetChildren();
	const std::size_t windowIndex = IndexOfGroup(top, "panels.window");

	REQUIRE(windowIndex != top.size());
	REQUIRE(windowIndex + 1 != top.size());
	CHECK(top[windowIndex + 1].GetText() == "view.resetLayout");
}

TEST_CASE("The File menu offers to trust the open folder, after closing it")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* file = FindSubmenu(tree, "File");
	REQUIRE(file != nullptr);

	std::vector<std::string> commands;

	for (const MenuItem& item : file->GetChildren())
	{
		if (item.GetKind() == MenuItem::Kind::Command)
		{
			commands.push_back(item.GetText());
		}
	}

	const std::vector<std::string>::const_iterator close = std::find(commands.begin(), commands.end(), "workspace.close");
	const std::vector<std::string>::const_iterator trust = std::find(commands.begin(), commands.end(), "workspace.toggleTrust");

	REQUIRE(close != commands.end());
	REQUIRE(trust != commands.end());
	CHECK(trust == close + 1);
}

TEST_CASE("The Help menu holds About alone, in every build")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* help = FindSubmenu(tree, "Help");

	REQUIRE(help != nullptr);
	REQUIRE(help->GetChildren().size() == 1);
	CHECK(help->GetChildren()[0].GetText() == "help.about");
}

#ifdef CORE_DEBUG
TEST_CASE("A Debug build ends the menu bar with a Debug menu holding the test log and the sample toasts")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* debug = FindSubmenu(tree, "Debug");

	REQUIRE(debug != nullptr);
	CHECK(tree.back().GetText() == "Debug");

	const std::vector<MenuItem>& debugItems = debug->GetChildren();
	REQUIRE(debugItems.size() == 3);
	CHECK(debugItems[0].GetText() == "debug.logTestEntries");
	CHECK(debugItems[1].GetKind() == MenuItem::Kind::Separator);

	const MenuItem* showToast = FindSubmenu(debugItems, "Show Toast");
	REQUIRE(showToast != nullptr);

	std::vector<std::string> toastCommandIds;
	for (const MenuItem& item : showToast->GetChildren())
	{
		toastCommandIds.push_back(item.GetText());
	}

	std::vector<std::string> sampleCommandIds;
	for (const Graphite::SampleToastCommand& sample : Graphite::kSampleToastCommands)
	{
		sampleCommandIds.emplace_back(sample.CommandId);
	}

	CHECK(toastCommandIds == sampleCommandIds);
}
#else
TEST_CASE("A build that is not Debug has no Debug menu, and Help ends the menu bar")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();

	CHECK(FindSubmenu(tree, "Debug") == nullptr);
	CHECK(tree.back().GetText() == "Help");
}
#endif

TEST_CASE("A terminal is opened from the View menu, right after the panels, without a menu of its own")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();

	CHECK(FindSubmenu(tree, "Terminal") == nullptr);

	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const std::vector<MenuItem>& top = view->GetChildren();

	REQUIRE(top.size() >= 3);
	CHECK(top[0].GetText() == "Panels");
	CHECK(top[1].GetKind() == MenuItem::Kind::Command);
	CHECK(top[1].GetText() == "terminal.new");

	const MenuItem* with = FindSubmenu(top, "New Terminal With");

	REQUIRE(with != nullptr);
	REQUIRE(with->GetChildren().size() == 1);
	CHECK(with->GetChildren()[0].GetKind() == MenuItem::Kind::Group);
	CHECK(with->GetChildren()[0].GetText() == "terminal.shells");
}

TEST_CASE("The Git menu lists fetch, pull, push and force push")
{
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* git = FindSubmenu(tree, "Git");

	REQUIRE(git != nullptr);

	std::vector<std::string> commands;

	for (const MenuItem& item : git->GetChildren())
	{
		commands.push_back(item.GetText());
	}

	CHECK(commands == std::vector<std::string>{"git.fetch", "git.pull", "git.push", "git.forcePush"});
}
