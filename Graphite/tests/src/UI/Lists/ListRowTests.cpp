#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>

namespace {
	constexpr float kFringe = 1.5f;
	constexpr float kOnTheEdge = 0.01f;
	constexpr float kIndent = 30.0f;
	constexpr float kInterfaceFontSize = 14.0f;
	constexpr float kListRowSpacing = 9.0f;
	constexpr float kHighlightInset = 6.0f;
	constexpr float kHighlightRounding = 5.0f;
	constexpr float kSelectedAlpha = 0.20f;
	constexpr float kPressedAlpha = 0.30f;
	constexpr float kHoverLiftTowardTheText = 0.06f;

	const ImVec4 kAccent(0.30f, 0.55f, 0.95f, 1.0f);
	const ImVec4 kBackground(0.08f, 0.08f, 0.09f, 1.0f);
	const ImVec4 kText(0.90f, 0.90f, 0.92f, 1.0f);

	ImU32 SelectedRowColour()
	{
		return ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, kSelectedAlpha));
	}

	ImU32 PressedRowColour()
	{
		return ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, kPressedAlpha));
	}

	ImU32 HoveredRowColour()
	{
		return ImGui::GetColorU32(ImLerp(kBackground, kText, kHoverLiftTowardTheText));
	}

	bool WithinFringe(float drawn, float expected)
	{
		return std::abs(drawn - expected) <= kFringe;
	}

	struct Extent
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<Extent> ExtentInColour(const ImDrawList& drawing, ImU32 colour, float fromY = -FLT_MAX,
										 float toY = FLT_MAX)
	{
		std::optional<Extent> extent;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.y < fromY || vertex.pos.y > toY)
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

	// A list as the panels hold them: a child window with no padding of its own, in a scheme whose accent,
	// background and text are told apart.
	struct ListScreen
	{
		ImGuiFrames Frames;
		ImRect List;
		float FirstRowTop = 0.0f;
		float EndX = 0.0f;
		float TextLine = 0.0f;
		float Pitch = 0.0f;

		ListScreen()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.FontSizeBase = kInterfaceFontSize;
			style.Colors[ImGuiCol_CheckMark] = kAccent;
			style.Colors[ImGuiCol_WindowBg] = kBackground;
			style.Colors[ImGuiCol_Text] = kText;
		}

		void Draw(const std::function<void()>& rows)
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
			ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			ImGui::BeginChild("##list", ImVec2(0.0f, 0.0f));
			const ImGuiWindow* list = ImGui::GetCurrentWindowRead();
			List = ImRect(list->InnerRect.Min, list->InnerRect.Max);
			FirstRowTop = ImGui::GetCursorScreenPos().y;
			EndX = Graphite::ListRowEnd();
			TextLine = ImGui::GetTextLineHeight();
			Pitch = Graphite::ListRowPitch();

			rows();

			ImGui::EndChild();
			ImGui::End();
		}

		void DrawFrame(const std::function<void()>& rows)
		{
			Frames.Next([&] { Draw(rows); });
		}

		float RowMiddle() const { return FirstRowTop + TextLine * 0.5f; }

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* panel = ImGui::FindWindowByName("Panel");
			REQUIRE(panel != nullptr);
			REQUIRE(!panel->DC.ChildWindows.empty());
			return *panel->DC.ChildWindows.back()->DrawList;
		}
	};

	void IndentedRows(bool firstSelected, bool secondSelected)
	{
		ImGui::Indent(kIndent);
		Graphite::ListRowSelectable("##first", firstSelected);
		Graphite::ListRowSelectable("##second", secondSelected);
		ImGui::Unindent(kIndent);
	}

	void CheckInsetFromTheListEdges(const Extent& highlight, const ListScreen& screen)
	{
		CHECK(WithinFringe(highlight.Left, screen.List.Min.x + kHighlightInset));
		CHECK(WithinFringe(highlight.Right, screen.List.Max.x - kHighlightInset));
	}
}

TEST_CASE("A row's pitch is the text line plus the list row spacing")
{
	ListScreen screen;
	float secondRowTop = 0.0f;

	screen.DrawFrame([&secondRowTop] {
		Graphite::ListRowSelectable("##first", false);
		secondRowTop = ImGui::GetCursorScreenPos().y;
	});

	CHECK(screen.TextLine == doctest::Approx(kInterfaceFontSize));
	CHECK(screen.Pitch == doctest::Approx(kInterfaceFontSize + kListRowSpacing));
	CHECK(secondRowTop - screen.FirstRowTop == doctest::Approx(screen.Pitch));
}

TEST_CASE("A row laid over by hand from its start or after it on the same line still leaves the next row a pitch below")
{
	ListScreen screen;
	float secondRowTop = 0.0f;
	float thirdRowTop = 0.0f;

	screen.DrawFrame([&secondRowTop, &thirdRowTop] {
		const ImVec2 firstRowStart = ImGui::GetCursorScreenPos();
		Graphite::ListRowSelectable("##first", false);
		ImGui::SetCursorScreenPos(firstRowStart);
		ImGui::TextUnformatted("Laid over from the start");

		secondRowTop = ImGui::GetCursorScreenPos().y;
		Graphite::ListRowSelectable("##second", false);
		ImGui::SameLine();
		ImGui::TextUnformatted("Laid after it");

		thirdRowTop = ImGui::GetCursorScreenPos().y;
	});

	CHECK(secondRowTop - screen.FirstRowTop == doctest::Approx(screen.Pitch));
	CHECK(thirdRowTop - secondRowTop == doctest::Approx(screen.Pitch));
}

TEST_CASE("The highlight is inset from the list's edges and rounded whatever the row's indent")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(true, false); });

	const std::optional<Extent> selected = ExtentInColour(ListScreen::DrawList(), SelectedRowColour());
	REQUIRE(selected.has_value());
	CheckInsetFromTheListEdges(*selected, screen);

	const std::optional<Extent> topEdge = ExtentInColour(ListScreen::DrawList(), SelectedRowColour(),
														 selected->Top - kOnTheEdge, selected->Top + kOnTheEdge);
	REQUIRE(topEdge.has_value());
	CHECK(topEdge->Left >= selected->Left + kHighlightRounding - kFringe);
	CHECK(topEdge->Right <= selected->Right - kHighlightRounding + kFringe);
}

TEST_CASE("The highlights of rows one under the other fill the pitch and touch")
{
	ListScreen screen;

	screen.DrawFrame([] { IndentedRows(true, false); });
	const std::optional<Extent> first = ExtentInColour(ListScreen::DrawList(), SelectedRowColour());

	screen.DrawFrame([] { IndentedRows(false, true); });
	const std::optional<Extent> second = ExtentInColour(ListScreen::DrawList(), SelectedRowColour());

	REQUIRE(first.has_value());
	REQUIRE(second.has_value());

	CHECK(WithinFringe(first->Bottom - first->Top, screen.Pitch));
	CHECK(WithinFringe(second->Top, first->Bottom));
}

TEST_CASE("A row's text line sits in the middle of its highlight")
{
	ListScreen screen;
	screen.DrawFrame([] { Graphite::ListRowSelectable("##row", true); });

	const std::optional<Extent> selected = ExtentInColour(ListScreen::DrawList(), SelectedRowColour());
	REQUIRE(selected.has_value());

	const float aboveTheText = screen.FirstRowTop - selected->Top;
	const float belowTheText = selected->Bottom - (screen.FirstRowTop + screen.TextLine);
	CHECK(std::abs(aboveTheText - belowTheText) <= kFringe);
}

TEST_CASE("A selected row is the accent at a fifth drawn by the row instead of ImGui's square header")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(true, false); });

	CHECK(DrawsInColour(ListScreen::DrawList(), SelectedRowColour()));
	CHECK_FALSE(DrawsInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Header)));
}

TEST_CASE("A hovered row is the background lifted toward the text and inset as a selected one")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(false, false); });

	ImGui::GetIO().AddMousePosEvent(screen.List.Max.x - 20.0f, screen.RowMiddle());
	screen.DrawFrame([] { IndentedRows(false, false); });

	const std::optional<Extent> hovered = ExtentInColour(ListScreen::DrawList(), HoveredRowColour());
	REQUIRE(hovered.has_value());
	CheckInsetFromTheListEdges(*hovered, screen);
	CHECK_FALSE(DrawsInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderHovered)));
}

TEST_CASE("A row held down under the pointer is the accent at three tenths")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(false, false); });

	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(screen.List.Max.x - 20.0f, screen.RowMiddle());
	screen.DrawFrame([] { IndentedRows(false, false); });
	io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	screen.DrawFrame([] { IndentedRows(false, false); });

	CHECK(DrawsInColour(ListScreen::DrawList(), PressedRowColour()));
	CHECK_FALSE(DrawsInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderActive)));
}

TEST_CASE("The row colours follow the scheme they are drawn in")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(true, false); });
	REQUIRE(DrawsInColour(ListScreen::DrawList(), SelectedRowColour()));

	const ImVec4 otherAccent(0.85f, 0.35f, 0.20f, 1.0f);
	ImGui::GetStyle().Colors[ImGuiCol_CheckMark] = otherAccent;
	screen.DrawFrame([] { IndentedRows(true, false); });

	CHECK(DrawsInColour(ListScreen::DrawList(),
						ImGui::GetColorU32(ImVec4(otherAccent.x, otherAccent.y, otherAccent.z, kSelectedAlpha))));
}

TEST_CASE("Drawing a list row leaves the global header colours as they were")
{
	ListScreen screen;
	constexpr ImGuiCol kHeaderColours[] = {ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
										   ImGuiCol_NavCursor};

	ImU32 before[std::size(kHeaderColours)] = {};
	ImU32 after[std::size(kHeaderColours)] = {};

	screen.DrawFrame([&] {
		for (std::size_t slot = 0; slot < std::size(kHeaderColours); ++slot)
		{
			before[slot] = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(kHeaderColours[slot]));
		}

		Graphite::ListRowSelectable("##row", true);

		for (std::size_t slot = 0; slot < std::size(kHeaderColours); ++slot)
		{
			after[slot] = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(kHeaderColours[slot]));
		}
	});

	for (std::size_t slot = 0; slot < std::size(kHeaderColours); ++slot)
	{
		CAPTURE(slot);
		CHECK(after[slot] == before[slot]);
	}
}

TEST_CASE("What stands at a list row's end keeps eight pixels from the list's right edge")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(false, false); });

	CHECK(screen.EndX == doctest::Approx(screen.List.Max.x - 8.0f));
	CHECK(Graphite::ListRowEndInset() == doctest::Approx(8.0f));
}

TEST_CASE("List rows leave a list that scrolls sideways without anything to scroll to")
{
	ImGuiFrames frames;
	const ImGuiWindow* scrolling = nullptr;

	const auto rows = [&scrolling] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(320.0f, 120.0f));
		ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		ImGui::BeginChild("##results", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

		const int rowCount = 30;
		for (int row = 0; row < rowCount; ++row)
		{
			ImGui::PushID(row);
			Graphite::ListRowSelectable("##row", false);
			ImGui::PopID();
		}

		scrolling = ImGui::GetCurrentWindowRead();
		ImGui::EndChild();
		ImGui::End();
	};

	const int framesForTheScrollbarsToSettle = 5;
	for (int frame = 0; frame < framesForTheScrollbarsToSettle; ++frame)
	{
		frames.Next(rows);
	}

	REQUIRE(scrolling != nullptr);
	CHECK(scrolling->ScrollbarY);
	CHECK_FALSE(scrolling->ScrollbarX);
	CHECK(scrolling->ScrollMax.x == doctest::Approx(0.0f));
}

TEST_CASE("The keyboard's mark on a row is drawn round its inset highlight")
{
	ImGuiFrames frames;
	bool focusTheRow = true;
	ImRect list;

	const auto rows = [&focusTheRow, &list] {
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
		ImGui::Begin("Rows", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		list = ImGui::GetCurrentWindowRead()->InnerRect;

		if (focusTheRow)
		{
			ImGui::SetKeyboardFocusHere();
			focusTheRow = false;
		}

		Graphite::ListRowSelectable("##first", false);
		Graphite::ListRowSelectable("##second", false);
		ImGui::End();
	};

	frames.Next(rows);
	frames.Next(rows);
	frames.PressKey(ImGuiKey_DownArrow, rows);
	frames.Next(rows);

	const ImGuiWindow* window = ImGui::FindWindowByName("Rows");
	REQUIRE(window != nullptr);

	const std::optional<Extent> mark = ExtentInColour(*window->DrawList, ImGui::GetColorU32(ImGuiCol_NavCursor));
	REQUIRE(mark.has_value());

	constexpr float kOutlineFringe = 2.0f * kFringe;
	CHECK(std::abs(mark->Left - (list.Min.x + kHighlightInset)) <= kOutlineFringe);
	CHECK(std::abs(mark->Right - (list.Max.x - kHighlightInset)) <= kOutlineFringe);
}

TEST_CASE("Right opens the tree node the keyboard is on and Left closes it")
{
	ImGuiFrames frames;
	bool open = false;
	bool focusTheRow = true;

	const auto tree = [&open, &focusTheRow] {
		ImGui::SetNextWindowFocus();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f));
		ImGui::Begin("Tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		if (focusTheRow)
		{
			ImGui::SetKeyboardFocusHere();
			focusTheRow = false;
		}

		open = Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth);

		if (open)
		{
			ImGui::TreePop();
		}

		ImGui::End();
	};

	frames.Next(tree);
	frames.Next(tree);
	frames.Next(tree);
	REQUIRE_FALSE(open);

	frames.PressKey(ImGuiKey_RightArrow, tree);
	frames.Next(tree);
	CHECK(open);

	frames.PressKey(ImGuiKey_LeftArrow, tree);
	frames.Next(tree);
	CHECK_FALSE(open);
}

TEST_CASE("A tree node under the pointer is highlighted inset from the list's edges and as tall as a list row")
{
	ListScreen screen;
	const auto rows = [] {
		ImGui::Indent(kIndent);
		if (Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			ImGui::TreePop();
		}
		ImGui::Unindent(kIndent);
	};
	screen.DrawFrame(rows);

	ImGui::GetIO().AddMousePosEvent(screen.List.Max.x - 20.0f, screen.RowMiddle());
	screen.DrawFrame(rows);

	const std::optional<Extent> hovered = ExtentInColour(ListScreen::DrawList(), HoveredRowColour());
	REQUIRE(hovered.has_value());

	CheckInsetFromTheListEdges(*hovered, screen);
	CHECK(WithinFringe(hovered->Bottom - hovered->Top, screen.Pitch));
}

TEST_CASE("A tree node is hovered from the list's left edge, its indent included")
{
	ListScreen screen;
	const auto rows = [] {
		ImGui::Indent(kIndent);
		if (Graphite::ChevronTreeNode("folder", ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			ImGui::TreePop();
		}
		ImGui::Unindent(kIndent);
	};
	screen.DrawFrame(rows);

	ImGui::GetIO().AddMousePosEvent(screen.List.Min.x + 4.0f, screen.RowMiddle());
	screen.DrawFrame(rows);

	CHECK(DrawsInColour(ListScreen::DrawList(), HoveredRowColour()));
}
