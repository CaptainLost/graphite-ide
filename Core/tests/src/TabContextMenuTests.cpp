#include <doctest/doctest.h>

#include "Core/ImGui/TabContextMenu.h"

#include <string>
#include <vector>

using Core::TabCloseAction;
using Core::TabsToClose;
using Names = std::vector<std::string>;

namespace {
	const Names kTabs{"main.cpp", "Settings", "Session", "Foo.h"};
}

TEST_CASE("Close picks only the clicked tab")
{
	CHECK(TabsToClose(kTabs, "Session", TabCloseAction::This) == Names{"Session"});
}

TEST_CASE("Close Others picks every tab but the clicked one, in tab order")
{
	CHECK(TabsToClose(kTabs, "main.cpp", TabCloseAction::Others) == Names{"Settings", "Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "Session", TabCloseAction::Others) == Names{"main.cpp", "Settings", "Foo.h"});
}

TEST_CASE("Close to the Right picks the tabs after the clicked one")
{
	CHECK(TabsToClose(kTabs, "Settings", TabCloseAction::ToTheRight) == Names{"Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "main.cpp", TabCloseAction::ToTheRight) == Names{"Settings", "Session", "Foo.h"});
	CHECK(TabsToClose(kTabs, "Foo.h", TabCloseAction::ToTheRight).empty());
}

TEST_CASE("Close All picks every tab")
{
	CHECK(TabsToClose(kTabs, "Settings", TabCloseAction::All) == kTabs);
}

TEST_CASE("A lone tab closes only itself")
{
	const Names alone{"Foo.h"};

	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::This) == alone);
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::Others).empty());
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::ToTheRight).empty());
	CHECK(TabsToClose(alone, "Foo.h", TabCloseAction::All) == alone);
}

TEST_CASE("A clicked tab missing from the list closes only itself, and only through Close")
{
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::This) == Names{"Gone"});
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::Others).empty());
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::ToTheRight).empty());
	CHECK(TabsToClose(kTabs, "Gone", TabCloseAction::All).empty());
}
