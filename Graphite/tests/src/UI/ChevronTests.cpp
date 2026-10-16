#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "UI/Chevron.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <optional>

namespace {
	constexpr float kFringe = 1.0f;
	const ImVec4 kDimmedRowText(0.9f, 0.6f, 0.2f, 1.0f);

	struct TreeDrawing
	{
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		const char* Label = "";
		std::optional<ImVec4> TextColour;
	};

	struct TreeScreen
	{
		ImGuiFrames Frames;
		TreeDrawing Drawing;
		float RowStartX = 0.0f;
		ImRect Row;
		bool Open = false;

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::Begin("Tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			if (Drawing.TextColour.has_value())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, *Drawing.TextColour);
			}

			RowStartX = ImGui::GetCursorScreenPos().x;
			Open = Graphite::ChevronTreeNode("node", Drawing.Flags, Drawing.Label);
			Row = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

			if (Drawing.TextColour.has_value())
			{
				ImGui::PopStyleColor();
			}

			if (Open)
			{
				ImGui::TreePop();
			}

			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([&] { Draw(); });
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName("Tree");
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}
	};

	struct HorizontalSpan
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
	};

	std::optional<HorizontalSpan> SpanInColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<HorizontalSpan> span;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			if (!span.has_value())
			{
				span = HorizontalSpan{};
			}

			span->Left = std::min(span->Left, vertex.pos.x);
			span->Right = std::max(span->Right, vertex.pos.x);
		}

		return span;
	}
}

TEST_CASE("A tree node is marked with the dim chevron, and draws nothing in the text colour where ImGui's arrow was")
{
	TreeScreen screen;
	screen.DrawFrame();

	CHECK(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled)));
	CHECK_FALSE(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text)));
}

TEST_CASE("A tree node's chevron sits where ImGui puts its arrow, clear of the label")
{
	TreeScreen screen;
	screen.DrawFrame();

	const std::optional<HorizontalSpan> chevron =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	REQUIRE(chevron.has_value());

	const float framePaddingX = ImGui::GetStyle().FramePadding.x;
	CHECK(chevron->Left >= screen.RowStartX + framePaddingX - kFringe);
	CHECK(chevron->Right <= screen.RowStartX + ImGui::GetTreeNodeToLabelSpacing() - framePaddingX + kFringe);
}

TEST_CASE("A tree node's label is drawn in the text colour, starting where ImGui starts it")
{
	TreeScreen screen;
	screen.Drawing.Label = "References";
	screen.DrawFrame();

	const std::optional<HorizontalSpan> label =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(label.has_value());

	CHECK(label->Left >= screen.RowStartX + ImGui::GetTreeNodeToLabelSpacing() - kFringe);
	CHECK(label->Left <= screen.RowStartX + ImGui::GetTreeNodeToLabelSpacing() + ImGui::GetFontSize());
}

TEST_CASE("A framed header keeps its frame, with the chevron and the label where ImGui puts its arrow and label")
{
	TreeScreen screen;
	screen.Drawing.Flags = ImGuiTreeNodeFlags_CollapsingHeader;
	screen.Drawing.Label = "References";
	screen.DrawFrame();

	const ImGuiStyle& style = ImGui::GetStyle();
	const float framedLabelStart = screen.RowStartX + ImGui::GetFontSize() + style.FramePadding.x * 3.0f;

	const std::optional<HorizontalSpan> chevron =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	const std::optional<HorizontalSpan> label =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(chevron.has_value());
	REQUIRE(label.has_value());

	CHECK(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Header)));
	CHECK(chevron->Left >= screen.RowStartX + style.FramePadding.x - kFringe);
	CHECK(chevron->Right <= framedLabelStart - style.FramePadding.x * 2.0f + kFringe);
	CHECK(label->Left >= framedLabelStart - kFringe);
	CHECK(label->Left <= framedLabelStart + ImGui::GetFontSize());
}

TEST_CASE("A tree node's label keeps a text colour pushed for its row, as a dimmed row pushes one")
{
	TreeScreen screen;
	screen.Drawing.Label = "References";
	screen.Drawing.TextColour = kDimmedRowText;
	screen.DrawFrame();

	CHECK(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(kDimmedRowText)));
}

TEST_CASE("A leaf has nothing to open, so it draws no chevron")
{
	TreeScreen screen;
	screen.Drawing.Flags |= ImGuiTreeNodeFlags_Leaf;
	screen.DrawFrame();

	CHECK_FALSE(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled)));
}

TEST_CASE("A click on a tree node's row opens it")
{
	TreeScreen screen;
	screen.DrawFrame();
	REQUIRE_FALSE(screen.Open);

	screen.Frames.Click(screen.Row.GetCenter(), [&] { screen.Draw(); });

	CHECK(screen.Open);
}
