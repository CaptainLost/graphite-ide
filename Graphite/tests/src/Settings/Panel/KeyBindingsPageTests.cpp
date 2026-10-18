#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "SettingsPageFrames.h"

#include "Settings/Panel/KeyBindingsPage.h"
#include "Settings/Panel/SettingsSearch.h"
#include "UI/Dialogs/ModalGate.h"

#include "Core/Command/Binding.h"
#include "Core/Command/Command.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

	struct KeyBindingsScreen
	{
		Core::CommandRegistry Commands;
		Core::Keymap Keys;
		Graphite::ModalGate Modals;
		std::size_t Changes = 0;
		Graphite::ActiveScheme Scheme;
		Graphite::KeyBindingsPage Page{Commands, Keys, Modals, Scheme, [this] { ++Changes; }};
		SettingsPageFrames Frames{Page};

		KeyBindingsScreen()
		{
			Commands.Add(Core::Command{.Id = "editor.toggleWordWrap", .Label = "Word Wrap"});
			Commands.Add(Core::Command{.Id = "file.save", .Label = "Save"});
			Commands.Add(Core::Command{.Id = "file.saveAll", .Label = "Save All"});

			Keys.SetDefault("file.save", Core::Binding{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiKey_S)});
			Keys.SetDefault("file.saveAll", Core::Binding{.Primary = Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S),
														  .Alternates = {Core::Chord::Key(ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_S)}});
		}
	};

	std::vector<std::string> TitlesOf(const std::vector<Graphite::SettingsRow>& rows)
	{
		std::vector<std::string> titles;

		for (const Graphite::SettingsRow& row : rows)
		{
			titles.push_back(row.Title);
		}

		return titles;
	}

	int RowsInBindingsTableIncludingHeader()
	{
		ImGuiContext& context = *ImGui::GetCurrentContext();

		for (int tableIndex = 0; tableIndex < context.Tables.GetMapSize(); ++tableIndex)
		{
			if (const ImGuiTable* table = context.Tables.TryGetMapData(tableIndex); table != nullptr && table->LastFrameActive == context.FrameCount)
			{
				return table->CurrentRow + 1;
			}
		}

		return 0;
	}
}

TEST_CASE("Commands are found word by word across their name and their shortcuts")
{
	KeyBindingsScreen screen;

	CHECK(TitlesOf(screen.Page.FindCommands(Graphite::SplitQueryWords("save"))) == std::vector<std::string>{"Save", "Save All"});
	CHECK(TitlesOf(screen.Page.FindCommands(Graphite::SplitQueryWords("save ctrl"))) == std::vector<std::string>{"Save", "Save All"});
	CHECK(TitlesOf(screen.Page.FindCommands(Graphite::SplitQueryWords("ctrl shift"))) == std::vector<std::string>{"Save All"});
	CHECK(TitlesOf(screen.Page.FindCommands(Graphite::SplitQueryWords("all ctrl alt"))) == std::vector<std::string>{"Save All"});
	CHECK(TitlesOf(screen.Page.FindCommands(Graphite::SplitQueryWords("WRAP"))) == std::vector<std::string>{"Word Wrap"});
	CHECK(screen.Page.FindCommands(Graphite::SplitQueryWords("save tabs")).empty());
}

TEST_CASE("A found command is a row with its id, its name, its shortcuts and no control")
{
	KeyBindingsScreen screen;

	const std::vector<Graphite::SettingsRow> saveAllRows = screen.Page.FindCommands(Graphite::SplitQueryWords("all"));
	const std::vector<Graphite::SettingsRow> wrapRows = screen.Page.FindCommands(Graphite::SplitQueryWords("wrap"));

	REQUIRE(saveAllRows.size() == 1);
	CHECK(saveAllRows[0].Key == "file.saveAll");
	CHECK(saveAllRows[0].Title == "Save All");
	CHECK(saveAllRows[0].Description == "Ctrl+Shift+S, Ctrl+Alt+S");
	CHECK(saveAllRows[0].OpenWith == "Save All");
	CHECK_FALSE(saveAllRows[0].DrawControl);

	REQUIRE(wrapRows.size() == 1);
	CHECK(wrapRows[0].Description == "No shortcut");
	CHECK(wrapRows[0].OpenWith == "Word Wrap");
}

TEST_CASE("A search with no words lists no command")
{
	KeyBindingsScreen screen;

	const std::vector<std::string> noWords;

	CHECK(screen.Page.FindCommands(noWords).empty());
}

TEST_CASE("The shortcuts block finds the commands inside itself")
{
	KeyBindingsScreen screen;

	const std::optional<Graphite::SettingsRow> row = screen.Frames.FindRow("Shortcuts");

	REQUIRE(row.has_value());
	REQUIRE(row->FindInside);
	CHECK(row->IsBlock());
	CHECK(TitlesOf(row->FindInside(Graphite::SplitQueryWords("wrap"))) == std::vector<std::string>{"Word Wrap"});
}

TEST_CASE("The table's filter shows the commands a search for the same words finds")
{
	KeyBindingsScreen screen;

	const std::string_view emptyQuery;

	screen.Page.OnOpenedFromSearch(emptyQuery);
	screen.Frames.DrawPageTwice();
	CHECK(RowsInBindingsTableIncludingHeader() == 1 + 3);

	screen.Page.OnOpenedFromSearch("SAVE ctrl");
	screen.Frames.DrawPageTwice();
	CHECK(RowsInBindingsTableIncludingHeader() == 1 + 2);

	screen.Page.OnOpenedFromSearch("shift save");
	screen.Frames.DrawPageTwice();
	CHECK(RowsInBindingsTableIncludingHeader() == 1 + 1);

	screen.Page.OnOpenedFromSearch("save tabs");
	screen.Frames.DrawPageTwice();
	CHECK(RowsInBindingsTableIncludingHeader() == 1);
}

TEST_CASE("Opening the page from a search filters its table by the text it is handed")
{
	KeyBindingsScreen screen;

	screen.Page.OnOpenedFromSearch("Close Tab");
	CHECK(screen.Page.GetFilter() == "Close Tab");

	screen.Page.OnOpenedFromSearch(std::string_view{});
	CHECK(screen.Page.GetFilter().empty());
}

TEST_CASE("Deactivating the page withdraws the restore question it asked")
{
	KeyBindingsScreen screen;
	const Core::Binding unbound;
	screen.Keys.Override("file.save", unbound);

	// WARNING: No frame of the whole page in between: the question's popup belongs to the window the
	// button was clicked in, and drawing another window would let it notice it is gone and withdraw itself.
	screen.Frames.ClickControl("Restore all defaults");
	REQUIRE(screen.Modals.IsBusy());

	screen.Page.OnDeactivated();

	CHECK_FALSE(screen.Modals.IsBusy());
}

TEST_CASE("The key bindings page draws wide and narrow without an ImGui assertion")
{
	KeyBindingsScreen screen;

	constexpr float wideWidth = 900.0f;
	constexpr float narrowWidth = 320.0f;

	screen.Frames.DrawPageTwice(wideWidth);
	screen.Frames.DrawPageTwice(narrowWidth);

	CHECK(screen.Changes == 0);
}

TEST_CASE("The key bindings scroll with their page rather than in a window of their own")
{
	KeyBindingsScreen screen;

	constexpr int extraCommandCount = 60;
	constexpr float pageWidth = 900.0f;

	for (int commandIndex = 0; commandIndex < extraCommandCount; ++commandIndex)
	{
		const std::string number = std::to_string(commandIndex);
		screen.Commands.Add(Core::Command{.Id = "test.command." + number, .Label = "Test command " + number});
	}

	// WARNING: A third frame is needed: the page's scroll range follows the content size of the frame before.
	screen.Frames.DrawPageTwice(pageWidth);
	screen.Frames.DrawPage(pageWidth);

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

namespace {

	constexpr float kFringe = 1.5f;
	constexpr float kHighlightInset = 6.0f;
	constexpr float kIntoTheFirstLine = 8.0f;
	constexpr float kIntoTheSlot = 10.0f;
	constexpr int kShortcutColumn = 1;
	constexpr int kSettleFrames = 3;

	struct BindingsTableShape
	{
		ImRect Ends;
		float LastRowTop = 0.0f;
		float LastRowBottom = 0.0f;
		ImVec2 OnLastRowsShortcut;
		const ImDrawList* Drawing = nullptr;
	};

	std::optional<BindingsTableShape> ShapeOfBindingsTable()
	{
		ImGuiContext& context = *ImGui::GetCurrentContext();

		for (int tableIndex = 0; tableIndex < context.Tables.GetMapSize(); ++tableIndex)
		{
			const ImGuiTable* table = context.Tables.TryGetMapData(tableIndex);

			if (table == nullptr || table->LastFrameActive != context.FrameCount)
			{
				continue;
			}

			return BindingsTableShape{
				.Ends = table->WorkRect,
				.LastRowTop = table->RowPosY1,
				.LastRowBottom = table->RowPosY2,
				.OnLastRowsShortcut = ImVec2(table->Columns[kShortcutColumn].WorkMinX + kIntoTheSlot, table->RowPosY1 + kIntoTheFirstLine),
				.Drawing = table->InnerWindow->DrawList};
		}

		return std::nullopt;
	}

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<Extent> extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			if (!extent.has_value())
			{
				extent = Extent{};
			}

			extent->Left = std::min(extent->Left, vertex.pos.x);
			extent->Right = std::max(extent->Right, vertex.pos.x);
			extent->Top = std::min(extent->Top, vertex.pos.y);
			extent->Bottom = std::max(extent->Bottom, vertex.pos.y);
		}

		return extent;
	}

	bool WithinFringe(float drawn, float expected)
	{
		return std::abs(drawn - expected) <= kFringe;
	}

	void CheckSpansTheRow(const Extent& highlight, const BindingsTableShape& shape)
	{
		CHECK(WithinFringe(highlight.Left, shape.Ends.Min.x + kHighlightInset));
		CHECK(WithinFringe(highlight.Right, shape.Ends.Max.x - kHighlightInset));
		CHECK(WithinFringe(highlight.Top, shape.LastRowTop));
		CHECK(WithinFringe(highlight.Bottom, shape.LastRowBottom));
	}
}

TEST_CASE("A key binding row under the pointer is lit across the table in the list colours, the slot under the pointer with it")
{
	KeyBindingsScreen screen;
	screen.Frames.DrawPageTwice();

	const std::optional<BindingsTableShape> before = ShapeOfBindingsTable();
	REQUIRE(before.has_value());
	CHECK(before->LastRowBottom - before->LastRowTop > Graphite::ListRowPitch());

	ImGui::GetIO().AddMousePosEvent(before->OnLastRowsShortcut.x, before->OnLastRowsShortcut.y);

	for (int frame = 0; frame < kSettleFrames; ++frame)
	{
		screen.Frames.DrawPage();
	}

	const std::optional<BindingsTableShape> shape = ShapeOfBindingsTable();
	REQUIRE(shape.has_value());

	const std::optional<Extent> hovered = ExtentInColour(*shape->Drawing, ImGui::GetColorU32(Graphite::CurrentListRowColours().Hovered));
	REQUIRE(hovered.has_value());
	CheckSpansTheRow(*hovered, *shape);
	CHECK_FALSE(DrawsInColour(*shape->Drawing, ImGui::GetColorU32(ImGuiCol_HeaderHovered)));
}

TEST_CASE("A key binding row whose shortcut is being recorded rests in the accent")
{
	KeyBindingsScreen screen;
	screen.Frames.DrawPageTwice();

	const std::optional<BindingsTableShape> before = ShapeOfBindingsTable();
	REQUIRE(before.has_value());

	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(before->OnLastRowsShortcut.x, before->OnLastRowsShortcut.y);
	screen.Frames.DrawPage();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	screen.Frames.DrawPage();
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	screen.Frames.DrawPage();
	screen.Frames.DrawPage();

	const std::optional<BindingsTableShape> shape = ShapeOfBindingsTable();
	REQUIRE(shape.has_value());

	const std::optional<Extent> selected = ExtentInColour(*shape->Drawing, ImGui::GetColorU32(Graphite::CurrentListRowColours().Selected));
	REQUIRE(selected.has_value());
	CheckSpansTheRow(*selected, *shape);
	CHECK_FALSE(DrawsInColour(*shape->Drawing, ImGui::GetColorU32(ImGuiCol_Header)));
}
