#include <doctest/doctest.h>

#include "SettingsPageFrames.h"

#include "Settings/Panel/KeyBindingsPage.h"
#include "UI/ModalGate.h"

#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <optional>
#include <string>

namespace {

	struct KeyBindingsScreen
	{
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Graphite::ModalGate Modals;
		std::size_t Changes = 0;
		Graphite::KeyBindingsPage Page{Commands, Keys, Modals, [this] { ++Changes; }};
		SettingsPageFrames Frames{Page};

		KeyBindingsScreen()
		{
			Commands.Add(Core::Command{.Id = "editor.toggleWordWrap", .Label = "Word Wrap"});
			Commands.Add(Core::Command{.Id = "file.save", .Label = "Save"});
			Commands.Add(Core::Command{.Id = "file.saveAll", .Label = "Save All"});
		}
	};
}

TEST_CASE("The key bindings page says how many commands its table would show for a query")
{
	KeyBindingsScreen screen;

	CHECK(screen.Page.DescribeCommandsMatching("save") == "2 commands match");
	CHECK(screen.Page.DescribeCommandsMatching("WRAP") == "1 command matches");
	CHECK(screen.Page.DescribeCommandsMatching("nothing like it").empty());
}

TEST_CASE("The shortcuts block is found by the commands in it")
{
	KeyBindingsScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Shortcuts");

	REQUIRE(row.has_value());
	REQUIRE(row->DescribeMatches);
	CHECK(row->IsBlock());
	CHECK(row->DescribeMatches("wrap") == "1 command matches");
}

TEST_CASE("Opening the page from a search filters its table by the query")
{
	KeyBindingsScreen screen;

	screen.Page.OnOpenedFromSearch("save");

	CHECK(screen.Page.GetFilter() == "save");
}

TEST_CASE("Deactivating the page withdraws the restore question it asked")
{
	KeyBindingsScreen screen;
	screen.Keys.Override("file.save", Core::Binding{});

	// No frame of the whole page in between: the question's popup belongs to the window the button
	// was clicked in, and drawing another window would let it notice it is gone and withdraw itself.
	screen.Frames.ClickControl("Restore all defaults");
	REQUIRE(screen.Modals.IsBusy());

	screen.Page.OnDeactivated();

	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("The key bindings page draws wide and narrow without an ImGui assertion")
{
	KeyBindingsScreen screen;

	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPageTwice(320.0f);

	CHECK(screen.Changes == 0);
}

TEST_CASE("The key bindings scroll with their page rather than in a window of their own")
{
	KeyBindingsScreen screen;

	for (int commandIndex = 0; commandIndex < 60; ++commandIndex)
	{
		const std::string number = std::to_string(commandIndex);
		screen.Commands.Add(Core::Command{.Id = "test.command." + number, .Label = "Test command " + number});
	}

	// A third frame: the page's scroll range follows the content size of the frame before.
	screen.Frames.DrawPageTwice(900.0f);
	screen.Frames.DrawPage(900.0f);

	const ImGuiWindow* page = ImGui::FindWindowByName("Settings");
	REQUIRE(page != nullptr);
	CHECK(page->ScrollMax.y > 0.0f);

	for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
	{
		const bool isInsidePage = window != page && window->RootWindow == page;

		if (isInsidePage)
		{
			CHECK_FALSE(window->ScrollbarY);
		}
	}
}
