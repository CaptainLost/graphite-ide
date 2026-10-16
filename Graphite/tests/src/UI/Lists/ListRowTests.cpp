#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <functional>
#include <optional>

namespace {
	constexpr float kFringe = 1.0f;
	constexpr float kIndent = 30.0f;

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

	// A list as the panels hold them: a child window with no padding of its own.
	struct ListScreen
	{
		ImGuiFrames Frames;
		ImRect List;
		float FirstRowTop = 0.0f;
		float EndX = 0.0f;

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

			rows();

			ImGui::EndChild();
			ImGui::End();
		}

		void DrawFrame(const std::function<void()>& rows)
		{
			Frames.Next([&] { Draw(rows); });
		}

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
}

TEST_CASE("A list row is highlighted across the whole list, from its left edge to its right, whatever its indent")
{
	ListScreen screen;
	screen.DrawFrame([] { IndentedRows(true, false); });

	const std::optional<Extent> selected = ExtentInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Header));
	REQUIRE(selected.has_value());

	CHECK(selected->Left <= screen.List.Min.x + kFringe);
	CHECK(selected->Right >= screen.List.Max.x - kFringe);
}

TEST_CASE("List rows are a text line and the item spacing apart, and the highlights of rows one under the other touch")
{
	ListScreen screen;
	const ImU32 selectedColour = ImGui::GetColorU32(ImGuiCol_Header);

	screen.DrawFrame([] { IndentedRows(true, false); });
	const float pitch = Graphite::ListRowPitch();
	const std::optional<Extent> first = ExtentInColour(ListScreen::DrawList(), selectedColour);

	screen.DrawFrame([] { IndentedRows(false, true); });
	const std::optional<Extent> second = ExtentInColour(ListScreen::DrawList(), selectedColour);

	REQUIRE(first.has_value());
	REQUIRE(second.has_value());

	CHECK(pitch == doctest::Approx(ImGui::GetTextLineHeightWithSpacing()));
	CHECK(first->Bottom - first->Top == doctest::Approx(pitch));
	CHECK(second->Top == doctest::Approx(first->Bottom));
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

TEST_CASE("A tree node under the pointer is highlighted across the whole list, as tall as a list row")
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

	const float rowMiddle = screen.FirstRowTop + ImGui::GetFontSize() * 0.5f;
	ImGui::GetIO().AddMousePosEvent(screen.List.Max.x - 20.0f, rowMiddle);
	screen.DrawFrame(rows);

	const std::optional<Extent> hovered =
		ExtentInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
	REQUIRE(hovered.has_value());

	CHECK(hovered->Left <= screen.List.Min.x + kFringe);
	CHECK(hovered->Right >= screen.List.Max.x - kFringe);
	CHECK(hovered->Bottom - hovered->Top == doctest::Approx(Graphite::ListRowPitch()).epsilon(0.05));
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

	ImGui::GetIO().AddMousePosEvent(screen.List.Min.x + 4.0f, screen.FirstRowTop + ImGui::GetFontSize() * 0.5f);
	screen.DrawFrame(rows);

	CHECK(ExtentInColour(ListScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_HeaderHovered)).has_value());
}
