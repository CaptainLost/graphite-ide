#include <doctest/doctest.h>

#include "Search/CommandPalette.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>

#include <string>
#include <vector>

using Graphite::ListPaletteEntries;
using Graphite::PaletteEntry;
using Graphite::RankPaletteEntries;

namespace {
	void Nothing() {}
}

TEST_CASE("The command palette lists enabled commands by label with their shortcut and leaves itself out")
{
	Core::CommandRegistry commands;
	commands.Add({.Id = "file.save", .Label = "Save\tCtrl+S", .Run = Nothing});
	commands.Add({.Id = "debug.toast", .Label = "Success toast", .ListLabel = "Debug: Show a success toast", .Run = Nothing});
	commands.Add({.Id = "git.push", .Label = "Push", .Run = Nothing, .Enabled = [] { return false; }});
	commands.Add({.Id = "view.nothing", .Label = "Does nothing"});
	commands.Add({.Id = "view.unlabelled", .Run = Nothing});
	commands.Add({.Id = "palette.open", .Label = "Command Palette...", .Run = Nothing});

	Core::Keymap keys;
	keys.SetDefault("file.save", {.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});

	const std::vector<PaletteEntry> entries = ListPaletteEntries(commands, keys, "palette.open");

	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Id == "debug.toast");
	CHECK(entries[0].Label == "Debug: Show a success toast");
	CHECK(entries[0].Shortcut.empty());
	CHECK(entries[1].Id == "file.save");
	CHECK(entries[1].Label == "Save");
	CHECK_FALSE(entries[1].Shortcut.empty());
}

TEST_CASE("The command palette ranks a label by a few of its letters")
{
	const std::vector<PaletteEntry> entries = {{.Id = "a", .Label = "Go to Line..."},
											   {.Id = "b", .Label = "Toggle Line Comment"},
											   {.Id = "c", .Label = "Save All"}};

	const std::vector<std::size_t> ranked = RankPaletteEntries("tlc", entries);

	REQUIRE_FALSE(ranked.empty());
	CHECK(ranked.front() == 1);
	CHECK(RankPaletteEntries("", entries) == std::vector<std::size_t>{0, 1, 2});
	CHECK(RankPaletteEntries("qqq", entries).empty());
}
