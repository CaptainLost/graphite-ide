#include <doctest/doctest.h>

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Theme.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ChromePalette.h"
#include "UI/TickBox.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstddef>
#include <optional>

namespace {

	constexpr float kFrameSeconds = 1.0f / 60.0f;
	const ImVec2 kTickBoxPosition(20.0f, 30.0f);
	const ImVec4 kChangedOutline(0.9f, 0.6f, 0.2f, 1.0f);

	// The share of the accent a ticked box's inside takes on, as TickBox.cpp tints it.
	constexpr float kTintShare = 0.16f;

	bool DrawsInColourWithin(const ImDrawList& drawing, ImU32 colour, ImVec2 lowest, ImVec2 highest)
	{
		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			const bool inside = vertex.pos.x >= lowest.x && vertex.pos.x <= highest.x && vertex.pos.y >= lowest.y &&
								vertex.pos.y <= highest.y;

			if (vertex.col == colour && inside)
			{
				return true;
			}
		}

		return false;
	}

	std::optional<float> TopOfColour(const ImDrawList& drawing, ImU32 colour)
	{
		std::optional<float> top;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour && (!top.has_value() || vertex.pos.y < *top))
			{
				top = vertex.pos.y;
			}
		}

		return top;
	}

	enum class OutlineChoice
	{
		Style,
		Custom
	};

	// One tick box in a window of its own, drawn a frame at a time as the application draws it.
	class TickBoxFrames
	{
	public:
		explicit TickBoxFrames(const char* label, OutlineChoice outline = OutlineChoice::Style)
			: m_Label(label), m_Outline(outline)
		{
			m_Context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = ImVec2(400.0f, 300.0f);
			io.DeltaTime = kFrameSeconds;
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigNavCursorVisibleAlways = true;
			io.Fonts->Build();
		}

		~TickBoxFrames() { ImGui::DestroyContext(m_Context); }

		// Paints the context the way the application paints itself with this scheme.
		void UseScheme(const Graphite::ResolvedScheme& scheme) { Core::Theme::Apply(Graphite::ChromePaletteFor(scheme)); }

		TickBoxFrames(const TickBoxFrames&) = delete;
		TickBoxFrames& operator=(const TickBoxFrames&) = delete;

		void Next(bool& value) { Draw(value, DrawMode::Normal); }

		void NextFocusing(bool& value) { Draw(value, DrawMode::Focus); }

		void NextDisabled(bool& value) { Draw(value, DrawMode::Disabled); }

		// Draws text after the box on the same line, as the review's file row does.
		void NextWithTextAfter(bool& value) { Draw(value, DrawMode::TextAfter); }

		// Draws the box in a collapsed window, which skips every item put in it.
		void NextInCollapsedWindow(bool& value) { Draw(value, DrawMode::Collapsed); }

		// A point inside the box, at its centre.
		void ClickBox(bool& value) { ClickAt(m_BoxCentre, value, DrawMode::Normal); }

		// A point on the label, a few pixels short of the item's right edge.
		void ClickLabel(bool& value) { ClickAt(m_LabelPoint, value, DrawMode::Normal); }

		void ClickBoxDisabled(bool& value) { ClickAt(m_BoxCentre, value, DrawMode::Disabled); }

		void PressKey(ImGuiKey key, bool& value)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(key, true);
			Draw(value, DrawMode::Normal);
			io.AddKeyEvent(key, false);
			Draw(value, DrawMode::Normal);
		}

		std::size_t GetFlipCount() const { return m_Flips; }

		ImVec2 GetItemSize() const { return m_ItemSize; }

		float GetReportedWidth() const { return m_ReportedWidth; }

		// How far below the tick box's top the text after it starts.
		float GetTextAfterOffset() const { return m_TextAfterOffset; }

		// Whether the box drew anything in the colour a hovered row is filled with.
		bool DrewHoveredRowColourOnBox() const { return m_HoveredRowColourOnBox; }

		// The top edge of a ticked box's tint, absent when nothing was tinted.
		std::optional<float> GetTintTop() const { return m_TintTop; }

		// Whether drawing the box added anything to the window's state storage.
		bool GrewStateStorage() const { return m_GrewStateStorage; }

	private:
		enum class DrawMode
		{
			Normal,
			Focus,
			Disabled,
			TextAfter,
			Collapsed
		};

		void ClickAt(const ImVec2& point, bool& value, DrawMode mode)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMousePosEvent(point.x, point.y);
			Draw(value, mode);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Draw(value, mode);
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Draw(value, mode);
		}

		void Draw(bool& value, DrawMode mode)
		{
			ImGui::NewFrame();
			Core::Animation::BeginFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f));

			if (mode == DrawMode::Collapsed)
			{
				const bool collapsed = true;
				ImGui::SetNextWindowCollapsed(collapsed);
			}

			ImGui::Begin("tick box");
			ImGui::SetCursorScreenPos(kTickBoxPosition);

			if (mode == DrawMode::Focus)
			{
				ImGui::SetKeyboardFocusHere();
			}

			const int storedBefore = ImGui::GetStateStorage()->Data.Size;

			ImGui::BeginDisabled(mode == DrawMode::Disabled);
			const bool flipped = DrawBox(value);
			ImGui::EndDisabled();

			m_GrewStateStorage = ImGui::GetStateStorage()->Data.Size > storedBefore;

			if (flipped)
			{
				++m_Flips;
			}

			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			const float side = Graphite::kTickBoxSize;
			m_BoxCentre = ImVec2(itemMin.x + side * 0.5f, (itemMin.y + itemMax.y) * 0.5f);
			m_LabelPoint = ImVec2(itemMax.x - 3.0f, (itemMin.y + itemMax.y) * 0.5f);
			m_ItemSize = ImVec2(itemMax.x - itemMin.x, itemMax.y - itemMin.y);
			m_ReportedWidth = Graphite::TickBoxWidth(m_Label);

			const ImDrawList& drawing = *ImGui::GetWindowDrawList();
			const ImVec2 boxBottomRight(itemMin.x + side, itemMax.y);
			m_HoveredRowColourOnBox =
				DrawsInColourWithin(drawing, ImGui::GetColorU32(ImGuiCol_HeaderHovered), itemMin, boxBottomRight);
			m_TintTop = TopOfColour(drawing, ImGui::GetColorU32(ImGuiCol_CheckMark, kTintShare));

			if (mode == DrawMode::TextAfter)
			{
				ImGui::SameLine();
				ImGui::TextUnformatted("M");
				m_TextAfterOffset = ImGui::GetItemRectMin().y - itemMin.y;
			}

			ImGui::End();
			ImGui::EndFrame();
		}

		bool DrawBox(bool& value)
		{
			switch (m_Outline)
			{
			case OutlineChoice::Style:
				return Graphite::DrawTickBox(m_Label, &value);
			case OutlineChoice::Custom:
				return Graphite::DrawTickBox(m_Label, &value, kChangedOutline);
			}

			return false;
		}

		const char* m_Label = nullptr;
		OutlineChoice m_Outline = OutlineChoice::Style;
		ImGuiContext* m_Context = nullptr;
		ImVec2 m_BoxCentre;
		ImVec2 m_LabelPoint;
		ImVec2 m_ItemSize;
		float m_ReportedWidth = 0.0f;
		float m_TextAfterOffset = 0.0f;
		bool m_HoveredRowColourOnBox = false;
		std::optional<float> m_TintTop;
		bool m_GrewStateStorage = false;
		std::size_t m_Flips = 0;
	};
}

TEST_CASE("A click on a tick box's box flips it once")
{
	TickBoxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.Next(value);

	frames.ClickBox(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("A click on a tick box's label flips it too")
{
	TickBoxFrames frames{"Auto-scroll"};
	bool value = true;
	frames.Next(value);

	frames.ClickLabel(value);

	CHECK_FALSE(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("A focused tick box flips on Space")
{
	TickBoxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.NextFocusing(value);
	frames.Next(value);

	frames.PressKey(ImGuiKey_Space, value);

	CHECK(value);
}

TEST_CASE("A disabled tick box does not flip on a click")
{
	TickBoxFrames frames{"Auto-scroll"};
	bool value = false;
	frames.NextDisabled(value);

	frames.ClickBoxDisabled(value);

	CHECK_FALSE(value);
	CHECK(frames.GetFlipCount() == 0);
}

TEST_CASE("A tick box is as tall as a frame and as wide as its box and spacing and label")
{
	TickBoxFrames frames{"Auto-scroll"};
	bool value = false;

	frames.Next(value);

	const float expected =
		Graphite::kTickBoxSize + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Auto-scroll").x;
	CHECK(frames.GetItemSize().x == doctest::Approx(expected));
	CHECK(frames.GetItemSize().y == doctest::Approx(ImGui::GetFrameHeight()));
	CHECK(frames.GetReportedWidth() == doctest::Approx(expected));
}

TEST_CASE("A tick box whose label is only an id is as wide as its box")
{
	TickBoxFrames frames{"##viewed"};
	bool value = false;

	frames.Next(value);

	CHECK(frames.GetItemSize().x == doctest::Approx(Graphite::kTickBoxSize));
	CHECK(frames.GetReportedWidth() == doctest::Approx(Graphite::kTickBoxSize));
}

TEST_CASE("Text after a tick box on the same line sits where it sits after a frame")
{
	TickBoxFrames frames{"##viewed"};
	bool value = false;

	frames.NextWithTextAfter(value);

	CHECK(frames.GetTextAfterOffset() == doctest::Approx(ImGui::GetStyle().FramePadding.y));
}

TEST_CASE("A tick box with its own outline colour flips like any other")
{
	TickBoxFrames frames{"##viewed", OutlineChoice::Custom};
	bool value = false;
	frames.Next(value);

	frames.ClickBox(value);

	CHECK(value);
	CHECK(frames.GetFlipCount() == 1);
}

TEST_CASE("An unticked tick box keeps an outline a hovered row does not hide in the dark scheme")
{
	TickBoxFrames frames{"##darkRow"};
	frames.UseScheme(Graphite::DarkScheme());
	bool value = false;

	frames.Next(value);

	CHECK_FALSE(frames.DrewHoveredRowColourOnBox());
}

TEST_CASE("An unticked tick box keeps an outline a hovered row does not hide in the light scheme")
{
	TickBoxFrames frames{"##lightRow"};
	frames.UseScheme(Graphite::LightScheme());
	bool value = false;

	frames.Next(value);

	CHECK_FALSE(frames.DrewHoveredRowColourOnBox());
}

TEST_CASE("A tick box sits on whole pixels so its outline stays sharp")
{
	TickBoxFrames frames{"##wholePixels"};
	bool value = true;

	frames.Next(value);

	REQUIRE(frames.GetTintTop().has_value());

	// An anti-aliased fill puts its solid vertices half a pixel inside the shape's edge.
	const float fringeInset = 0.5f;
	const float edge = *frames.GetTintTop() - fringeInset;
	CHECK(edge == doctest::Approx(std::floor(edge)));
}

TEST_CASE("A tick box in a window that skips its items keeps no state and does not flip")
{
	TickBoxFrames frames{"##skipped"};
	bool value = false;

	frames.NextInCollapsedWindow(value);

	CHECK_FALSE(frames.GrewStateStorage());
	CHECK(frames.GetFlipCount() == 0);
}
