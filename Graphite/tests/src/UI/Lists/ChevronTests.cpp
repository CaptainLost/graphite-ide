#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>

namespace {
	constexpr float kFringe = 1.0f;
	constexpr float kListRowSpacing = 6.0f;
	constexpr float kInterfaceItemSpacing = 5.0f;
	constexpr float kChevronBoxLeft = 12.0f;
	constexpr float kChevronBoxToLabel = 5.0f;
	const ImVec4 kDimmedRowText(0.9f, 0.6f, 0.2f, 1.0f);

	struct TreeDrawing
	{
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		const char* Label = "";
		const char* Note = "";
		std::optional<ImVec4> TextColour;
	};

	struct TreeScreen
	{
		ImGuiFrames Frames;
		TreeDrawing Drawing;
		float RowStartX = 0.0f;
		float RowStartY = 0.0f;
		float NextRowStartY = 0.0f;
		float TextLine = 0.0f;
		float TextTop = 0.0f;
		ImRect Row;
		bool Open = false;

		TreeScreen() { ImGui::GetStyle().FontSizeBase = Core::kInterfaceFontSize; }

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(300.0f, 200.0f));
			ImGui::GetStyle().ItemSpacing.y = kInterfaceItemSpacing;
			ImGui::Begin("Tree", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			{
				const Graphite::ListBodyScope listBody;

				if (Drawing.TextColour.has_value())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, *Drawing.TextColour);
				}

				RowStartX = ImGui::GetCursorScreenPos().x;
				RowStartY = ImGui::GetCursorScreenPos().y;
				TextLine = ImGui::GetTextLineHeight();
				TextTop = Graphite::ListRowTextTop(RowStartY);
				Open = Graphite::ChevronTreeNode("node", Drawing.Flags, Drawing.Label, Drawing.Note);
				Row = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

				if (Drawing.TextColour.has_value())
				{
					ImGui::PopStyleColor();
				}

				if (Open)
				{
					ImGui::TreePop();
				}

				NextRowStartY = ImGui::GetCursorScreenPos().y;
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

	std::optional<HorizontalSpan> SpanInColour(const ImDrawList& drawing, ImU32 colour, float fromX = -FLT_MAX)
	{
		std::optional<HorizontalSpan> span;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.x < fromX)
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

	struct VerticalSpan
	{
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
	};

	std::optional<VerticalSpan> VerticalSpanInColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<VerticalSpan> span;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour)
			{
				continue;
			}

			if (!span.has_value())
			{
				span = VerticalSpan{};
			}

			span->Top = std::min(span->Top, vertex.pos.y);
			span->Bottom = std::max(span->Bottom, vertex.pos.y);
		}

		return span;
	}
}

TEST_CASE("A tree node is a list row's pitch tall and the next row starts that far below it")
{
	TreeScreen screen;
	screen.DrawFrame();

	const float pitch = screen.TextLine + kListRowSpacing;
	CHECK(screen.Row.GetHeight() == doctest::Approx(pitch));
	CHECK(screen.NextRowStartY - screen.RowStartY == doctest::Approx(pitch));
}

TEST_CASE("A tree node's chevron and label sit on the text line in the middle of its row")
{
	constexpr float kTwentyTwo = 22.0f;

	TreeScreen screen;
	screen.Drawing.Label = "References";
	screen.DrawFrame();

	const float textLineTop = screen.TextTop;
	const float textLineBottom = screen.TextTop + screen.TextLine;

	CHECK(screen.Row.Min.y == doctest::Approx(screen.RowStartY));
	CHECK(screen.Row.GetHeight() == doctest::Approx(kTwentyTwo));

	const float aboveTheText = textLineTop - screen.Row.Min.y;
	const float belowTheText = screen.Row.Max.y - textLineBottom;
	CHECK(std::abs(aboveTheText - belowTheText) <= kFringe);

	for (const ImGuiCol drawnIn : {ImGuiCol_Text, ImGuiCol_TextDisabled})
	{
		CAPTURE(drawnIn);

		const std::optional<VerticalSpan> glyphs = VerticalSpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(drawnIn));
		REQUIRE(glyphs.has_value());
		CHECK(glyphs->Top >= textLineTop - kFringe);
		CHECK(glyphs->Bottom <= textLineBottom + kFringe);
	}
}

TEST_CASE("A tree node is marked with the dim chevron, and draws nothing in the text colour where ImGui's arrow was")
{
	TreeScreen screen;
	screen.DrawFrame();

	CHECK(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled)));
	CHECK_FALSE(DrawsInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text)));
}

TEST_CASE("A tree node's chevron sits in its box twelve pixels into the row, clear of the label")
{
	TreeScreen screen;
	screen.DrawFrame();

	const std::optional<HorizontalSpan> chevron =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	REQUIRE(chevron.has_value());

	const float boxLeft = screen.RowStartX + Graphite::TreeRowChevronBoxLeft();
	CHECK(Graphite::TreeRowChevronBoxLeft() == doctest::Approx(kChevronBoxLeft));
	CHECK(chevron->Left >= boxLeft - kFringe);
	CHECK(chevron->Right <= boxLeft + Graphite::TreeRowChevronBoxWidth() + kFringe);
	CHECK(chevron->Right < screen.RowStartX + Graphite::TreeRowLabelOffset());
}

TEST_CASE("A tree node's label is drawn in the text colour, five pixels past its chevron's box")
{
	TreeScreen screen;
	screen.Drawing.Label = "References";
	screen.DrawFrame();

	const std::optional<HorizontalSpan> label =
		SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(label.has_value());

	CHECK(Graphite::TreeRowLabelOffset() ==
		  doctest::Approx(Graphite::TreeRowChevronBoxLeft() + Graphite::TreeRowChevronBoxWidth() + kChevronBoxToLabel));
	CHECK(label->Left >= screen.RowStartX + Graphite::TreeRowLabelOffset() - kFringe);
	CHECK(label->Left <= screen.RowStartX + Graphite::TreeRowLabelOffset() + ImGui::GetFontSize());
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

TEST_CASE("A tree node's note is drawn dim after its label, plain or framed")
{
	for (const ImGuiTreeNodeFlags flags : {ImGuiTreeNodeFlags_None, ImGuiTreeNodeFlags_CollapsingHeader})
	{
		CAPTURE(flags);

		TreeScreen screen;
		screen.Drawing.Flags = flags;
		screen.Drawing.Label = "D:/Repos/proj";
		screen.Drawing.Note = "$1.20";
		screen.DrawFrame();

		const std::optional<HorizontalSpan> label =
			SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text));
		REQUIRE(label.has_value());

		const std::optional<HorizontalSpan> note =
			SpanInColour(TreeScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled), label->Right);
		REQUIRE(note.has_value());

		CHECK(note->Left > label->Right);
	}
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
