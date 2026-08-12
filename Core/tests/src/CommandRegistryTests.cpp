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

    Command* added = commands.Add({.id = "editor.zoomIn",
                                   .label = "Zoom In",
                                   .run = [&runs] { ++runs; }});

    REQUIRE(added != nullptr);
    REQUIRE(commands.Find("editor.zoomIn") != nullptr);
    CHECK(commands.Find("editor.zoomIn")->label == "Zoom In");
    CHECK(commands.Find("editor.zoomOut") == nullptr);
}

TEST_CASE("Add rejects a duplicate id")
{
    // The same contract PanelManager::Add has. Two commands under one id would make
    // "which one does the shortcut run" a question about registration order.
    CommandRegistry commands;
    commands.Add({.id = "editor.zoomIn", .label = "Zoom In", .run = [] {}});

    Command* duplicate = commands.Add({.id = "editor.zoomIn", .label = "Other", .run = [] {}});

    CHECK(duplicate == nullptr);
    CHECK(commands.Find("editor.zoomIn")->label == "Zoom In");
}

TEST_CASE("Run invokes the command and reports that it did")
{
    CommandRegistry commands;
    int runs = 0;
    commands.Add({.id = "editor.zoomIn", .label = "Zoom In", .run = [&runs] { ++runs; }});

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

    commands.Add({.id = "workspace.close",
                  .label = "Close Workspace",
                  .run = [&runs] { ++runs; },
                  .enabled = [&workspaceOpen] { return workspaceOpen; }});

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
    commands.Add({.id = "help.logTestEntries", .label = "Log test entries", .run = [&runs] { ++runs; }});

    CHECK(commands.Run("help.logTestEntries"));
    CHECK(runs == 1);
}

TEST_CASE("InGroup returns the group's members in registration order")
{
    // Registration order is the panel order in the View menu, which is the order panels
    // were added in — so this is the guarantee that reordering Add calls reorders the menu.
    CommandRegistry commands;
    commands.Add({.id = "panel.files", .label = "Files", .group = "panels", .run = [] {}});
    commands.Add({.id = "editor.zoomIn", .label = "Zoom In", .run = [] {}});
    commands.Add({.id = "panel.console", .label = "Console", .group = "panels", .run = [] {}});

    const std::vector<const Command*> members = commands.InGroup("panels");

    REQUIRE(members.size() == 2);
    CHECK(members[0]->id == "panel.files");
    CHECK(members[1]->id == "panel.console");
}

TEST_CASE("InGroup on an unknown group is empty rather than an error")
{
    CommandRegistry commands;
    commands.Add({.id = "editor.zoomIn", .label = "Zoom In", .run = [] {}});

    CHECK(commands.InGroup("panels").empty());
}

TEST_CASE("A registered command keeps its address as more are added")
{
    // Add returns a pointer callers hold on to. A vector of Command would invalidate it
    // on the next Add, which is exactly the kind of failure that survives review.
    CommandRegistry commands;
    Command* first = commands.Add({.id = "a", .label = "A", .run = [] {}});

    for (int index = 0; index < 64; ++index)
    {
        commands.Add({.id = "filler" + std::to_string(index), .label = "Filler", .run = [] {}});
    }

    CHECK(first == commands.Find("a"));
    CHECK(first->label == "A");
}
