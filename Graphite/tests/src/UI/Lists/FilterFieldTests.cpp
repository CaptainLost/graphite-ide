#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/Lists/FilterField.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <IconsLucide.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace {
	constexpr float kFringe = 1.0f;
	constexpr float kFieldWidth = 220.0f;
	constexpr float kMagnifierSize = 12.0f;
	constexpr float kMagnifierToText = 6.0f;
	constexpr float kLineFringe = 1.5f;

	struct HorizontalSpan
	{
		float Left = FLT_MAX;
		float Right = -FLT_MAX;
	};

	std::optional<HorizontalSpan> SpanInColour(const ImDrawList& drawing, ImU32 colour, float fromX = -FLT_MAX,
											   float toX = FLT_MAX)
	{
		std::optional<HorizontalSpan> span;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.x < fromX || vertex.pos.x > toX)
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

	enum class TextKind
	{
		Buffer,
		String
	};

	struct FilterScreen
	{
		ImGuiFrames Frames;
		std::array<char, 64> Buffer{};
		std::string Text;
		TextKind Kind = TextKind::Buffer;
		std::optional<float> Width = kFieldWidth;
		ImVec2 FieldMin;
		float NextItemX = 0.0f;
		bool Edited = false;

		void Type(std::string_view text)
		{
			Buffer.fill('\0');
			std::copy_n(text.data(), std::min(text.size(), Buffer.size() - 1), Buffer.data());
			Text = text;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
			ImGui::Begin("Filter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			FieldMin = ImGui::GetCursorScreenPos();
			const Graphite::FilterFieldLook look{.Hint = "Filter files", .Width = Width};

			if (Kind == TextKind::Buffer)
			{
				Edited = Graphite::DrawFilterField("##filter", Buffer.data(), Buffer.size(), look) || Edited;
			}
			else
			{
				Edited = Graphite::DrawFilterField("##filter", Text, look) || Edited;
			}

			ImGui::SameLine();
			NextItemX = ImGui::GetCursorScreenPos().x;
			ImGui::Dummy(ImVec2(1.0f, 1.0f));

			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([&] { Draw(); });
		}

		void Click(ImVec2 at)
		{
			Frames.Click(at, [&] { Draw(); });
		}

		ImVec2 FieldCentre() const
		{
			return ImVec2(FieldMin.x + kFieldWidth * 0.5f, FieldMin.y + ImGui::GetFrameHeight() * 0.5f);
		}

		ImVec2 ClearButtonCentre() const
		{
			const float inset = std::floor((ImGui::GetFrameHeight() - Core::IconButtonSize()) * 0.5f);
			return ImVec2(FieldMin.x + kFieldWidth - inset - Core::IconButtonSize() * 0.5f,
						  FieldMin.y + ImGui::GetFrameHeight() * 0.5f);
		}

		float MagnifierStart() const { return FieldMin.x + ImGui::GetStyle().FramePadding.x; }

		float TextStart() const { return MagnifierStart() + kMagnifierSize + kMagnifierToText; }

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName("Filter");
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}
	};
}

TEST_CASE("An empty filter field shows the dim magnifier at its start and the dim hint after it")
{
	FilterScreen screen;
	screen.DrawFrame();

	const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const std::optional<HorizontalSpan> magnifier =
		SpanInColour(FilterScreen::DrawList(), dim, -FLT_MAX, screen.TextStart() - kFringe);
	const std::optional<HorizontalSpan> hint = SpanInColour(FilterScreen::DrawList(), dim, screen.TextStart() - kFringe);
	REQUIRE(magnifier.has_value());
	REQUIRE(hint.has_value());

	CHECK(magnifier->Left >= screen.MagnifierStart() - kFringe);
	CHECK(magnifier->Right <= screen.MagnifierStart() + kMagnifierSize + kFringe);
	CHECK(hint->Left <= screen.TextStart() + ImGui::GetFontSize());
}

TEST_CASE("A filter field is the mockup's: a field high, a line-colour border, the magnifier twelve pixels eight in and the text six after it")
{
	FilterScreen screen;
	Core::Theme::Apply(Core::Theme::DefaultPalette());
	Core::Theme::SetMetrics(ImGui::GetStyle());

	std::optional<HorizontalSpan> line;
	std::optional<HorizontalSpan> magnifier;
	float fieldHeight = 0.0f;
	float glyphHeight = 0.0f;

	screen.Frames.Next([&] {
		ImGui::PushFont(nullptr, Core::kInterfaceFontSize);
		screen.Draw();
		fieldHeight = ImGui::GetFrameHeight();
		ImGui::PopFont();

		// Between the window's own left and right edges, which are drawn in the line colour as well.
		const ImDrawList& drawing = FilterScreen::DrawList();
		line = SpanInColour(drawing, ImGui::GetColorU32(ImGuiCol_Border), screen.FieldMin.x - kLineFringe,
							screen.FieldMin.x + kFieldWidth + kLineFringe);
		magnifier = SpanInColour(drawing, ImGui::GetColorU32(ImGuiCol_TextDisabled), -FLT_MAX, screen.TextStart() - kFringe);

		float top = FLT_MAX;
		float bottom = -FLT_MAX;
		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == ImGui::GetColorU32(ImGuiCol_TextDisabled) && vertex.pos.x < screen.TextStart() - kFringe)
			{
				top = std::min(top, vertex.pos.y);
				bottom = std::max(bottom, vertex.pos.y);
			}
		}
		glyphHeight = bottom - top;
	});

	CHECK(fieldHeight == doctest::Approx(26.0f));

	REQUIRE(line.has_value());
	CHECK(std::fabs(line->Left - screen.FieldMin.x) <= kLineFringe);
	CHECK(std::fabs(line->Right - (screen.FieldMin.x + kFieldWidth)) <= kLineFringe);

	REQUIRE(magnifier.has_value());
	CHECK(magnifier->Left >= screen.MagnifierStart() - kFringe);
	CHECK(magnifier->Right <= screen.MagnifierStart() + kMagnifierSize + kFringe);
	CHECK(glyphHeight <= kMagnifierSize + kFringe);
}

TEST_CASE("Text in a filter field starts after the magnifier, which stays")
{
	FilterScreen screen;
	screen.Type("pols");
	screen.DrawFrame();

	const std::optional<HorizontalSpan> text =
		SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_Text));
	const std::optional<HorizontalSpan> magnifier =
		SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	REQUIRE(text.has_value());
	REQUIRE(magnifier.has_value());

	CHECK(text->Left >= screen.TextStart() - kFringe);
	CHECK(magnifier->Right < screen.TextStart());
}

TEST_CASE("A filter field is one frame across the whole width it is given, with or without text")
{
	for (const char* typed : {"", "pols"})
	{
		CAPTURE(typed);

		FilterScreen screen;
		screen.Type(typed);
		screen.DrawFrame();

		const std::optional<HorizontalSpan> frame =
			SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_FrameBg));
		REQUIRE(frame.has_value());

		CHECK(std::fabs(frame->Left - screen.FieldMin.x) <= kFringe);
		CHECK(std::fabs(frame->Right - (screen.FieldMin.x + kFieldWidth)) <= kFringe);
	}
}

TEST_CASE("What follows a filter field on its line starts past the field's end, clear button or not")
{
	for (const char* typed : {"", "pols"})
	{
		CAPTURE(typed);

		FilterScreen screen;
		screen.Type(typed);
		screen.DrawFrame();

		CHECK(screen.NextItemX == doctest::Approx(screen.FieldMin.x + kFieldWidth + ImGui::GetStyle().ItemSpacing.x));
	}
}

TEST_CASE("A filter field with no width given takes the rest of the line")
{
	FilterScreen screen;
	screen.Width.reset();
	screen.DrawFrame();

	const std::optional<HorizontalSpan> frame =
		SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_FrameBg));
	REQUIRE(frame.has_value());

	const ImGuiWindow* window = ImGui::FindWindowByName("Filter");
	REQUIRE(window != nullptr);
	CHECK(std::fabs(frame->Right - window->WorkRect.Max.x) <= kFringe);
}

TEST_CASE("A click on the clear button inside a filter field empties it and reports the change")
{
	FilterScreen screen;
	screen.Type("pols");
	screen.DrawFrame();

	screen.Click(screen.ClearButtonCentre());

	CHECK(screen.Buffer[0] == '\0');
	CHECK(screen.Edited);
}

TEST_CASE("A click on the clear button while the field is being typed in empties it, and it stays empty")
{
	FilterScreen screen;
	screen.Type("pols");
	screen.DrawFrame();
	screen.Click(screen.FieldCentre());
	REQUIRE(ImGui::GetActiveID() == ImGui::FindWindowByName("Filter")->GetID("##filter"));
	screen.Edited = false;

	screen.Click(screen.ClearButtonCentre());
	screen.DrawFrame();

	CHECK(screen.Buffer[0] == '\0');
	CHECK(screen.Edited);
}

TEST_CASE("A filter field given no room draws nothing wider than the room, and an input no wider than the field")
{
	for (const float room : {0.0f, -40.0f, 10.0f})
	{
		CAPTURE(room);

		FilterScreen screen;
		screen.Type("pols");
		screen.Width = room;
		screen.DrawFrame();

		const std::optional<HorizontalSpan> frame =
			SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_FrameBg));
		REQUIRE(frame.has_value());

		CHECK(frame->Left >= screen.FieldMin.x - kFringe);
		CHECK(frame->Right <= screen.FieldMin.x + std::max(room, 1.0f) + kFringe);
		CHECK(screen.NextItemX <= screen.FieldMin.x + std::max(room, 1.0f) + ImGui::GetStyle().ItemSpacing.x + kFringe);
	}
}

TEST_CASE("A filter field's frame ends on a whole pixel, where ImGui lays the field out")
{
	FilterScreen screen;
	screen.Width = 180.6f;
	screen.DrawFrame();

	const std::optional<HorizontalSpan> frame =
		SpanInColour(FilterScreen::DrawList(), ImGui::GetColorU32(ImGuiCol_FrameBg));
	REQUIRE(frame.has_value());

	CHECK(frame->Right <= screen.FieldMin.x + 180.0f);
	CHECK(screen.NextItemX == doctest::Approx(screen.FieldMin.x + 180.0f + ImGui::GetStyle().ItemSpacing.x));
}

TEST_CASE("The clear button empties a filter field kept in a string as well")
{
	FilterScreen screen;
	screen.Kind = TextKind::String;
	screen.Type("pols");
	screen.DrawFrame();

	screen.Click(screen.ClearButtonCentre());

	CHECK(screen.Text.empty());
	CHECK(screen.Edited);
}

TEST_CASE("An empty filter field has no clear button: a click at its right end goes into the field")
{
	FilterScreen screen;
	screen.DrawFrame();

	screen.Click(screen.ClearButtonCentre());

	CHECK(ImGui::GetActiveID() == ImGui::FindWindowByName("Filter")->GetID("##filter"));
}

TEST_CASE("Esc in a filter field being typed in empties it and reports the change")
{
	FilterScreen screen;
	screen.Type("pols");
	screen.DrawFrame();
	screen.Click(screen.FieldCentre());
	screen.Edited = false;

	screen.Frames.PressKey(ImGuiKey_Escape, [&] { screen.Draw(); });

	CHECK(screen.Buffer[0] == '\0');
	CHECK(screen.Edited);
}

TEST_CASE("The width left for a filter field leaves room for the icon buttons after it")
{
	ImGuiFrames frames;

	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
		ImGui::Begin("Filter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		const float room = Core::IconButtonSize() + ImGui::GetStyle().ItemSpacing.x;
		CHECK(Graphite::FilterFieldWidthBefore(2) == doctest::Approx(ImGui::GetContentRegionAvail().x - room * 2.0f));

		ImGui::End();
	});
}
