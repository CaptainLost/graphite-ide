#include <doctest/doctest.h>

#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"

#include <string>
#include <vector>

using Core::Command;
using Core::CommandRegistry;

TEST_CASE("Add registers a command and allows finding it by id")
{
	CommandRegistry commands;
	int runCount = 0;

	Command* added = commands.Add({.Id = "editor.zoomIn",
								   .Label = "Zoom In",
								   .Run = [&runCount] { ++runCount; }});

	REQUIRE(added != nullptr);
	REQUIRE(commands.Find("editor.zoomIn") != nullptr);
	CHECK(commands.Find("editor.zoomIn")->Label == "Zoom In");
	CHECK(commands.Find("editor.zoomOut") == nullptr);
}

TEST_CASE("Add rejects a duplicate id so which command a shortcut runs never depends on registration order")
{
	CommandRegistry commands;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [] {}});

	Command* duplicate = commands.Add({.Id = "editor.zoomIn", .Label = "Other", .Run = [] {}});

	CHECK(duplicate == nullptr);
	CHECK(commands.Find("editor.zoomIn")->Label == "Zoom In");
}

TEST_CASE("Run invokes the command and reports that it did")
{
	CommandRegistry commands;
	int runCount = 0;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In", .Run = [&runCount] { ++runCount; }});

	CHECK(commands.Run("editor.zoomIn"));
	CHECK(runCount == 1);
}

TEST_CASE("Run refuses an unknown id")
{
	CommandRegistry commands;

	CHECK_FALSE(commands.Run("nothing.here"));
}

TEST_CASE("Run refuses a command registered without an action")
{
	CommandRegistry commands;
	commands.Add({.Id = "editor.zoomIn", .Label = "Zoom In"});

	CHECK_FALSE(commands.Run("editor.zoomIn"));
}

TEST_CASE("Run refuses a command its own predicate disables just as its greyed-out menu item does")
{
	CommandRegistry commands;
	bool workspaceOpen = false;
	int runCount = 0;

	commands.Add({.Id = "workspace.close",
				  .Label = "Close Workspace",
				  .Run = [&runCount] { ++runCount; },
				  .Enabled = [&workspaceOpen] { return workspaceOpen; }});

	CHECK_FALSE(commands.Run("workspace.close"));
	CHECK(runCount == 0);

	workspaceOpen = true;

	CHECK(commands.Run("workspace.close"));
	CHECK(runCount == 1);
}

TEST_CASE("A command without predicates always runs")
{
	CommandRegistry commands;
	int runCount = 0;
	commands.Add({.Id = "help.logTestEntries", .Label = "Log test entries", .Run = [&runCount] { ++runCount; }});

	CHECK(commands.Run("help.logTestEntries"));
	CHECK(runCount == 1);
}

TEST_CASE("InGroup returns the group's members in registration order which is the order a menu lists them in")
{
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

TEST_CASE("A registered command keeps the address Add returned as more are added")
{
	constexpr int kFillerCount = 64;

	CommandRegistry commands;
	Command* firstCommand = commands.Add({.Id = "a", .Label = "A", .Run = [] {}});

	for (int fillerIndex = 0; fillerIndex < kFillerCount; ++fillerIndex)
	{
		commands.Add({.Id = "filler" + std::to_string(fillerIndex), .Label = "Filler", .Run = [] {}});
	}

	CHECK(firstCommand == commands.Find("a"));
	CHECK(firstCommand->Label == "A");
}

TEST_CASE("RunFromMenu runs the command's menu variant when it has one while Run still runs the plain one as a chord does")
{
	CommandRegistry commands;
	int plainRunCount = 0;
	int menuRunCount = 0;
	commands.Add({.Id = "edit.copy",
				  .Label = "Copy",
				  .Run = [&plainRunCount] { ++plainRunCount; },
				  .RunFromMenu = [&menuRunCount] { ++menuRunCount; }});

	CHECK(commands.RunFromMenu("edit.copy"));
	CHECK(menuRunCount == 1);
	CHECK(plainRunCount == 0);

	CHECK(commands.Run("edit.copy"));
	CHECK(plainRunCount == 1);
	CHECK(menuRunCount == 1);
}

TEST_CASE("RunFromMenu falls back to Run and is gated like it")
{
	CommandRegistry commands;
	int runCount = 0;
	bool isEnabled = true;
	commands.Add({.Id = "editor.zoomIn",
				  .Label = "Zoom In",
				  .Run = [&runCount] { ++runCount; },
				  .Enabled = [&isEnabled] { return isEnabled; }});

	CHECK(commands.RunFromMenu("editor.zoomIn"));
	CHECK(runCount == 1);

	isEnabled = false;
	CHECK_FALSE(commands.RunFromMenu("editor.zoomIn"));
	CHECK(runCount == 1);
	CHECK_FALSE(commands.RunFromMenu("nothing.here"));
}
