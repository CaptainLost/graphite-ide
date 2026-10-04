#include <doctest/doctest.h>

#include "Core/Command/CommandRegistry.h"

#include <string>
#include <vector>

using Core::Command;
using Core::CommandRegistry;

TEST_CASE("Add registers a command and allows finding it by id")
{
	CommandRegistry commands;
	int runs = 0;

	Command* added = commands.Add({.Id = "editor.zoomIn",
								   .Label = "Zoom In",
								   .Run = [&runs] { ++runs; }});

	REQUIRE(added != nullptr);
	REQUIRE(commands.Find("editor.zoomIn") != nullptr);
	CHECK(commands.Find("editor.zoomIn")->Label == "Zoom In");
	CHECK(commands.Find("editor.zoomOut") == nullptr);
}

TEST_CASE("Add rejects a duplicate id")
{
	// The same contract PanelManager::Add has. Two commands under one id would make
	// "which one does the shortcut run" a question about registration order.
	CommandRegistry commands;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [] {}});

	Command* duplicate = commands.Add({.Id = "editor.zoomIn", .Label = "Other", .Run = [] {}});

	CHECK(duplicate == nullptr);
	CHECK(commands.Find("editor.zoomIn")->Label == "Zoom In");
}

TEST_CASE("Run invokes the command and reports that it did")
{
	CommandRegistry commands;
	int runs = 0;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [&runs] { ++runs; }});

	CHECK(commands.Run("editor.zoomIn"));
	CHECK(runs == 1);
}

TEST_CASE("Run refuses an unknown id")
{
	CommandRegistry commands;

	CHECK_FALSE(commands.Run("nothing.here"));
}

TEST_CASE("Run refuses a command its own predicate disables")
{
	// The gate that keeps a binding from doing what the greyed-out menu item will not:
	// without it, a chord on "Close Workspace" would close one that is not open.
	CommandRegistry commands;
	bool workspaceOpen = false;
	int runs = 0;

	commands.Add({.Id = "workspace.close",
				  .Label = "Close Workspace",
				  .Run = [&runs] { ++runs; },
				  .Enabled = [&workspaceOpen] { return workspaceOpen; }});

	CHECK_FALSE(commands.Run("workspace.close"));
	CHECK(runs == 0);

	workspaceOpen = true;

	CHECK(commands.Run("workspace.close"));
	CHECK(runs == 1);
}

TEST_CASE("A command without predicates always runs")
{
	CommandRegistry commands;
	int runs = 0;
	commands.Add({.Id = "help.logTestEntries", .Label = "Log test entries", .Run = [&runs] { ++runs; }});

	CHECK(commands.Run("help.logTestEntries"));
	CHECK(runs == 1);
}

TEST_CASE("InGroup returns the group's members in registration order")
{
	// Registration order is the panel order in the View menu, which is the order panels
	// were added in — so this is the guarantee that reordering Add calls reorders the menu.
	CommandRegistry commands;
	commands.Add({.Id = "panel.files", .Label = "Files", .Group = "panels", .Run = [] {}});
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [] {}});
	commands.Add({.Id = "panel.console", .Label = "Console", .Group = "panels", .Run = [] {}});

	const std::vector<const Command*> members = commands.InGroup("panels");

	REQUIRE(members.size() == 2);
	CHECK(members[0]->Id == "panel.files");
	CHECK(members[1]->Id == "panel.console");
}

TEST_CASE("InGroup on an unknown group is empty rather than an error")
{
	CommandRegistry commands;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [] {}});

	CHECK(commands.InGroup("panels").empty());
}

TEST_CASE("A registered command keeps its address as more are added")
{
	// Add returns a pointer callers hold on to. A vector of Command would invalidate it
	// on the next Add, which is exactly the kind of failure that survives review.
	CommandRegistry commands;
	Command* first = commands.Add({.Id = "a", .Label = "A", .Run = [] {}});

	for (int index = 0; index < 64; ++index)
	{
		commands.Add({.Id = "filler" + std::to_string(index), .Label = "Filler", .Run = [] {}});
	}

	CHECK(first == commands.Find("a"));
	CHECK(first->Label == "A");
}

TEST_CASE("RunFromMenu runs the command's menu variant when it has one")
{
	CommandRegistry commands;
	int keyRuns = 0;
	int menuRuns = 0;
	commands.Add({.Id = "edit.copy",
				  .Label = "Copy",
				  .Run = [&keyRuns] { ++keyRuns; },
				  .RunFromMenu = [&menuRuns] { ++menuRuns; }});

	CHECK(commands.RunFromMenu("edit.copy"));
	CHECK(menuRuns == 1);
	CHECK(keyRuns == 0);

	// A chord still runs the plain variant.
	CHECK(commands.Run("edit.copy"));
	CHECK(keyRuns == 1);
	CHECK(menuRuns == 1);
}

TEST_CASE("RunFromMenu falls back to Run and is gated like it")
{
	CommandRegistry commands;
	int runs = 0;
	bool enabled = true;
	commands.Add({.Id = "editor.zoomIn",
				  .Label = "Zoom In",
				  .Run = [&runs] { ++runs; },
				  .Enabled = [&enabled] { return enabled; }});

	CHECK(commands.RunFromMenu("editor.zoomIn"));
	CHECK(runs == 1);

	enabled = false;
	CHECK_FALSE(commands.RunFromMenu("editor.zoomIn"));
	CHECK(runs == 1);
	CHECK_FALSE(commands.RunFromMenu("nothing.here"));
}
