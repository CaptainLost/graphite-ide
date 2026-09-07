#include <doctest/doctest.h>

#include "App/GraphiteMenu.h"

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
				return &item;
		}

		return nullptr;
	}

	// A section and everything drawn under it, up to the next section. This is the block the
	// reader sees under one heading, so it is what the tests below make claims about.
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
					break;

				inside = item.GetText() == heading;
				continue;
			}

			if (inside)
				body.push_back(&item);
		}

		return body;
	}

	std::vector<std::string> SectionHeadings(const std::vector<MenuItem>& items)
	{
		std::vector<std::string> headings;

		for (const MenuItem& item : items)
		{
			if (item.GetKind() == MenuItem::Kind::Section)
				headings.push_back(item.GetText());
		}

		return headings;
	}
}

TEST_CASE("The panels submenu holds every panel, under a heading per role")
{
	// The sections are filled from the group a panel registers itself into, so a panel added
	// later appears under its own heading without this tree being touched.
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const MenuItem* panels = FindSubmenu(view->GetChildren(), "Panels");

	REQUIRE(panels != nullptr);
	CHECK(SectionHeadings(panels->GetChildren())
		  == std::vector<std::string>{"Navigate", "Tools", "Git"});

	for (const auto& [heading, group] : {std::pair{"Navigate", "panels.navigate"},
										 std::pair{"Tools", "panels.tools"},
										 std::pair{"Git", "panels.git"}})
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
	// A shortcut is worth printing only if it is read without being hunted for, so these
	// keep the level the menu opens on.
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	std::vector<std::string> commands;

	for (const MenuItem& item : view->GetChildren())
	{
		if (item.GetKind() == MenuItem::Kind::Command)
			commands.push_back(item.GetText());
	}

	CHECK(commands == std::vector<std::string>{"editor.toggleWordWrap", "editor.zoomIn",
											   "editor.zoomOut", "editor.resetZoom",
											   "view.resetLayout"});
}

TEST_CASE("The explorer's ignored-file modes are reached through the panel that obeys them")
{
	// They used to hang under a submenu called "Git", which in this menu already means the
	// git panel. Both names on the way down now say whose setting this is: the explorer's,
	// about the files git ignores.
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
		modes.push_back(item.GetText());

	CHECK(modes == std::vector<std::string>{"git.ignored.show", "git.ignored.dim",
											"git.ignored.hide"});
}

TEST_CASE("The settings panel is reached from the top level, not from among the panels")
{
	// It is how the application is configured rather than a view of the work, so it keeps
	// the company of Reset Layout instead of hiding behind Panels with the rest.
	const std::vector<MenuItem> tree = Graphite::BuildMenuTree();
	const MenuItem* view = FindSubmenu(tree, "View");

	REQUIRE(view != nullptr);

	const MenuItem* panels = FindSubmenu(view->GetChildren(), "Panels");

	REQUIRE(panels != nullptr);

	for (const MenuItem& item : panels->GetChildren())
		CHECK(item.GetText() != "panels.window");

	const std::vector<MenuItem>& top = view->GetChildren();
	const auto window = std::find_if(top.begin(), top.end(), [](const MenuItem& item) {
		return item.GetKind() == MenuItem::Kind::Group && item.GetText() == "panels.window";
	});

	REQUIRE(window != top.end());
	REQUIRE(std::next(window) != top.end());
	CHECK(std::next(window)->GetText() == "view.resetLayout");
}
