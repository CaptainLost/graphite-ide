#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "FakeImageTexture.h"
#include "ImGuiFrames.h"

#include "Image/PictureCanvas.h"
#include "Image/Zoom.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

using Graphite::PictureBackground;
using Graphite::PixelPoint;

namespace {

	constexpr ImTextureID kPictureTextureId = 77;
	constexpr ImVec2 kWindowSize(600.0f, 400.0f);
	constexpr ImVec2 kFitsAtFullSize(100.0f, 80.0f);
	constexpr ImVec2 kTooLargeToFit(2000.0f, 1000.0f);
	constexpr ImVec2 kTinyPicture(20.0f, 15.0f);
	constexpr ImVec2 kLargePicture(200.0f, 200.0f);
	constexpr ImVec2 kFarAway(1200.0f, 700.0f);
	constexpr ImU32 kDarkBackground = IM_COL32(13, 14, 16, 255);
	constexpr ImU32 kLightBackground = IM_COL32(243, 244, 246, 255);
	constexpr ImU32 kGridColour = IM_COL32(0, 0, 0, 128);
	constexpr float kCheckerSide = 8.0f;
	constexpr float kCheckerNearShare = 0.06f;
	constexpr float kCheckerFarShare = 0.14f;
	constexpr float kGridZoom = 8.0f;
	constexpr float kStepBelowTheGrid = 6.0f;
	constexpr float kTallerThanTheWindow = 400.0f;
	constexpr float kScrollableBelow = 200.0f;
	constexpr int kPastDoubleClickFrames = 30;
	constexpr const char* kWindow = "Canvas";
	constexpr const char* kCanvasId = "##picture";

	struct CanvasScreen
	{
		explicit CanvasScreen(ImVec2 picture) : Texture(kPictureTextureId, picture)
		{
			Input.Texture = &Texture;
			Input.Picture = picture;
		}

		void DrawWindow()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::Begin(kWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			if (Before)
			{
				Before();
			}

			Result = Graphite::DrawPictureCanvas(kCanvasId, Zoom, Input);

			if (After)
			{
				After();
			}

			ImGui::End();
		}

		std::function<void()> Drawing()
		{
			return [this] { DrawWindow(); };
		}

		void Draw() { Frames.Next(Drawing()); }

		void MoveTo(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Draw();
		}

		void Press(bool down)
		{
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, down);
			Draw();
		}

		void DoubleClick(ImVec2 at)
		{
			Frames.Click(at, Drawing());
			Frames.Click(at, Drawing());
		}

		void Idle(int frames)
		{
			for (int frame = 0; frame < frames; ++frame)
			{
				Draw();
			}
		}

		ImRect Placed() const { return Graphite::PlacePicture(Zoom, Input.Picture, Result.Area); }

		std::optional<PixelPoint> PixelAt(ImVec2 point) const
		{
			return Graphite::PixelUnder(point, Zoom, Input.Picture, Result.Area);
		}

		void ShowAt(float zoom)
		{
			Zoom = Graphite::ZoomState{.Zoom = zoom, .Pan = ImVec2(0.0f, 0.0f), .Opened = true};
			Draw();
		}

		static ImGuiWindow& Window()
		{
			ImGuiWindow* window = ImGui::FindWindowByName(kWindow);
			REQUIRE(window != nullptr);
			return *window;
		}

		static const ImDrawList& DrawList() { return *Window().DrawList; }

		ImGuiFrames Frames;
		FakeImageTexture Texture;
		Graphite::ZoomState Zoom;
		Graphite::PictureCanvasInput Input;
		Graphite::PictureCanvasResult Result;
		std::function<void()> Before;
		std::function<void()> After;
	};

	bool HasVertexAt(const ImDrawList& drawing, ImU32 colour, ImVec2 at)
	{
		return std::ranges::any_of(drawing.VtxBuffer, [colour, at](const ImDrawVert& vertex) {
			return vertex.col == colour && vertex.pos.x == at.x && vertex.pos.y == at.y;
		});
	}

	// Where the vertices drawn in `colour` are, its alpha exact and each colour channel within the rounding of a blend.
	std::vector<ImVec2> VerticesNear(const ImDrawList& drawing, ImU32 colour)
	{
		std::vector<ImVec2> found;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (ChannelOf(vertex.col, IM_COL32_A_SHIFT) == ChannelOf(colour, IM_COL32_A_SHIFT) &&
				WithinOneLevel(vertex.col, colour))
			{
				found.push_back(vertex.pos);
			}
		}

		return found;
	}

	bool Contains(const std::vector<ImVec2>& points, ImVec2 point)
	{
		return std::ranges::any_of(points, [point](ImVec2 each) { return each.x == point.x && each.y == point.y; });
	}

	bool AllInside(const std::vector<ImVec2>& points, const ImRect& rect)
	{
		return std::ranges::all_of(points, [&rect](ImVec2 point) {
			return point.x >= rect.Min.x && point.x <= rect.Max.x && point.y >= rect.Min.y && point.y <= rect.Max.y;
		});
	}

	ImU32 Blend(ImVec4 from, ImVec4 to, float share)
	{
		return ImGui::ColorConvertFloat4ToU32(ImLerp(from, to, share));
	}

	// The colours current as the canvas draws, pushed after the window began so that the window's own background
	// keeps its colour and cannot pass for a square.
	void DrawInColours(CanvasScreen& screen, ImVec4 content, ImVec4 text)
	{
		screen.Before = [content, text] {
			ImGui::PushStyleColor(ImGuiCol_WindowBg, content);
			ImGui::PushStyleColor(ImGuiCol_Text, text);
		};
		screen.After = [] { ImGui::PopStyleColor(2); };
	}

	// WARNING: the two bodies differ so the linker cannot fold the callbacks into one address.
	int g_NearestSamplerCalls = 0;
	int g_LinearSamplerCalls = 0;

	void SetSamplerNearest(const ImDrawList*, const ImDrawCmd*)
	{
		++g_NearestSamplerCalls;
	}

	void SetSamplerLinear(const ImDrawList*, const ImDrawCmd*)
	{
		++g_LinearSamplerCalls;
	}

	void InstallSamplerCallbacks()
	{
		ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
		platformIo.DrawCallback_SetSamplerNearest = &SetSamplerNearest;
		platformIo.DrawCallback_SetSamplerLinear = &SetSamplerLinear;
	}

	bool DrawsPicture(const ImDrawCmd& command)
	{
		return command.UserCallback == nullptr && command.ElemCount > 0 &&
			   command.TexRef == ImTextureRef(kPictureTextureId);
	}

	bool Calls(const ImDrawCmd& command, ImDrawCallback callback)
	{
		return command.UserCallback == callback;
	}

	std::vector<ImDrawCmd> Commands()
	{
		const ImDrawList& drawing = CanvasScreen::DrawList();
		return {drawing.CmdBuffer.begin(), drawing.CmdBuffer.end()};
	}

	int CallbackCount(const std::vector<ImDrawCmd>& commands)
	{
		return static_cast<int>(
			std::ranges::count_if(commands, [](const ImDrawCmd& command) { return command.UserCallback != nullptr; }));
	}
}

TEST_CASE("The canvas opens a picture that fits at 100%")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Zoom.Pan = ImVec2(40.0f, 30.0f);

	screen.Draw();

	CHECK(screen.Zoom.Opened);
	CHECK(screen.Zoom.Zoom == doctest::Approx(1.0f));
	CHECK(screen.Zoom.Pan.x == 0.0f);
	CHECK(screen.Zoom.Pan.y == 0.0f);

	screen.Zoom.Zoom = 2.0f;
	screen.Draw();

	CHECK(screen.Zoom.Zoom == doctest::Approx(2.0f));
}

TEST_CASE("The canvas opens a picture that does not fit at Fit")
{
	CanvasScreen screen(kTooLargeToFit);

	screen.Draw();

	CHECK(screen.Zoom.Opened);
	CHECK(screen.Zoom.Zoom == doctest::Approx(Graphite::FitZoom(kTooLargeToFit, screen.Result.Area.GetSize(), Graphite::FitMargin())));
	CHECK(screen.Zoom.Zoom < 1.0f);
}

TEST_CASE("The canvas waits for room before it opens a picture")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Before = [] { ImGui::Dummy(ImVec2(1.0f, kTallerThanTheWindow)); };

	screen.Draw();

	CHECK_FALSE(screen.Zoom.Opened);

	screen.Before = nullptr;
	screen.Draw();

	CHECK(screen.Zoom.Opened);
	CHECK(screen.Zoom.Zoom == doctest::Approx(1.0f));
}

TEST_CASE("A wheel notch over the picture zooms one step around the pointer")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.After = [] { ImGui::Dummy(ImVec2(1.0f, kScrollableBelow)); };
	screen.Draw();
	screen.Draw();
	REQUIRE(CanvasScreen::Window().ScrollMax.y > 0.0f);

	const ImVec2 pointer(screen.Placed().Min.x + 30.5f, screen.Placed().Min.y + 20.5f);
	screen.MoveTo(pointer);
	const std::optional<PixelPoint> before = screen.PixelAt(pointer);
	REQUIRE(before.has_value());

	ImGui::GetIO().AddMouseWheelEvent(0.0f, 1.0f);
	screen.Draw();

	CHECK(screen.Zoom.Zoom == doctest::Approx(1.5f));
	CHECK(screen.PixelAt(pointer) == before);

	ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
	screen.Draw();

	CHECK(screen.Zoom.Zoom == doctest::Approx(1.0f));
	CHECK(screen.PixelAt(pointer) == before);
	// A window at its top would scroll on the notch down, had the canvas left the wheel to it.
	CHECK(CanvasScreen::Window().Scroll.y == 0.0f);
}

TEST_CASE("Dragging the picture pans it")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Draw();

	const ImVec2 start = screen.Placed().GetCenter();
	screen.MoveTo(start);
	screen.Press(true);
	screen.MoveTo(ImVec2(start.x + 10.0f, start.y + 5.0f));
	screen.MoveTo(ImVec2(start.x + 30.0f, start.y + 20.0f));
	screen.Press(false);

	CHECK(screen.Zoom.Pan.x == doctest::Approx(30.0f));
	CHECK(screen.Zoom.Pan.y == doctest::Approx(20.0f));

	const ImVec2 again = screen.Placed().GetCenter();
	screen.MoveTo(again);
	screen.Press(true);
	screen.MoveTo(ImVec2(again.x + 900.0f, again.y));
	screen.Press(false);

	CHECK(screen.Placed().Min.x == doctest::Approx(screen.Result.Area.Max.x - Graphite::PanKeepVisible()));
}

TEST_CASE("A double click switches between Fit and 100%")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Draw();
	screen.Zoom.Pan = ImVec2(10.0f, 5.0f);
	screen.Draw();
	const float fit = Graphite::FitZoom(kFitsAtFullSize, screen.Result.Area.GetSize(), Graphite::FitMargin());
	REQUIRE(fit > 1.0f);

	screen.DoubleClick(screen.Placed().GetCenter());

	CHECK(screen.Zoom.Zoom == doctest::Approx(fit));
	CHECK(screen.Zoom.Pan.x == 0.0f);
	CHECK(screen.Zoom.Pan.y == 0.0f);

	screen.Idle(kPastDoubleClickFrames);
	screen.Zoom.Pan = ImVec2(-10.0f, 5.0f);
	screen.DoubleClick(screen.Placed().GetCenter());

	CHECK(screen.Zoom.Zoom == doctest::Approx(1.0f));
	CHECK(screen.Zoom.Pan.x == 0.0f);
	CHECK(screen.Zoom.Pan.y == 0.0f);
}

TEST_CASE("The background is drawn only under the picture")
{
	for (const auto& [background, colour] :
		 {std::pair(PictureBackground::Dark, kDarkBackground), std::pair(PictureBackground::Light, kLightBackground)})
	{
		CAPTURE(static_cast<int>(background));
		CanvasScreen screen(kFitsAtFullSize);
		screen.Input.Background = background;

		screen.Draw();

		const ImRect placed = screen.Placed();
		const std::optional<HorizontalExtent> across = HorizontalExtentInColour(CanvasScreen::DrawList(), colour);
		const std::optional<VerticalExtent> down = VerticalExtentInColour(CanvasScreen::DrawList(), colour);
		REQUIRE(across.has_value());
		REQUIRE(down.has_value());
		CHECK(VerticesInColour(CanvasScreen::DrawList(), colour) == 4);
		CHECK(across->Left == placed.Min.x);
		CHECK(across->Right == placed.Max.x);
		CHECK(down->Top == placed.Min.y);
		CHECK(down->Bottom == placed.Max.y);
	}
}

TEST_CASE("The checkerboard follows the theme's content background")
{
	const std::pair<ImVec4, ImVec4> themes[] = {
		{ImVec4(0.2f, 0.3f, 0.5f, 1.0f), ImVec4(1.0f, 0.9f, 0.8f, 1.0f)},
		{ImVec4(0.95f, 0.9f, 0.85f, 1.0f), ImVec4(0.1f, 0.2f, 0.4f, 1.0f)},
	};

	for (const auto& [content, text] : themes)
	{
		CAPTURE(content.x);
		CanvasScreen screen(kFitsAtFullSize);
		DrawInColours(screen, content, text);

		for (const float zoom : {1.0f, 3.0f})
		{
			CAPTURE(zoom);
			screen.ShowAt(zoom);
			const ImRect placed = screen.Placed();

			const std::vector<ImVec2> nearSquares =
				VerticesNear(CanvasScreen::DrawList(), Blend(content, text, kCheckerNearShare));
			const std::vector<ImVec2> farSquares =
				VerticesNear(CanvasScreen::DrawList(), Blend(content, text, kCheckerFarShare));
			REQUIRE_FALSE(nearSquares.empty());
			REQUIRE_FALSE(farSquares.empty());
			CHECK(AllInside(nearSquares, placed));
			CHECK(AllInside(farSquares, placed));

			// The square right of the top-left one, the same size at every zoom.
			CHECK(Contains(farSquares, ImVec2(placed.Min.x + kCheckerSide, placed.Min.y)));
			CHECK(Contains(farSquares, ImVec2(placed.Min.x + kCheckerSide * 2.0f, placed.Min.y + kCheckerSide)));
		}
	}

	CanvasScreen zoomedIn(kTooLargeToFit);
	const auto& [content, text] = themes[0];
	DrawInColours(zoomedIn, content, text);
	zoomedIn.ShowAt(kGridZoom);

	const std::vector<ImVec2> nearSquares =
		VerticesNear(CanvasScreen::DrawList(), Blend(content, text, kCheckerNearShare));
	const std::vector<ImVec2> farSquares =
		VerticesNear(CanvasScreen::DrawList(), Blend(content, text, kCheckerFarShare));
	REQUIRE_FALSE(nearSquares.empty());
	REQUIRE_FALSE(farSquares.empty());
	CHECK(AllInside(nearSquares, zoomedIn.Result.Area));
	CHECK(AllInside(farSquares, zoomedIn.Result.Area));
}

TEST_CASE("The picture is sampled nearest from 100% and smoothly below")
{
	CanvasScreen screen(kFitsAtFullSize);
	InstallSamplerCallbacks();

	for (const float zoom : {1.0f, 2.0f})
	{
		CAPTURE(zoom);
		screen.ShowAt(zoom);

		const std::vector<ImDrawCmd> commands = Commands();
		const auto nearest = std::ranges::find_if(
			commands, [](const ImDrawCmd& command) { return Calls(command, &SetSamplerNearest); });

		REQUIRE(nearest != commands.end());
		REQUIRE(std::distance(nearest, commands.end()) >= 3);
		CHECK(DrawsPicture(*std::next(nearest)));
		CHECK(Calls(*std::next(nearest, 2), &SetSamplerLinear));
		CHECK(CallbackCount(commands) == 2);
	}

	screen.ShowAt(0.5f);

	const std::vector<ImDrawCmd> commands = Commands();
	CHECK(std::ranges::count_if(commands, DrawsPicture) == 1);
	CHECK(CallbackCount(commands) == 0);
}

TEST_CASE("The pixel grid is drawn from 800% and not at 600%")
{
	CanvasScreen screen(kTinyPicture);

	screen.ShowAt(kGridZoom);

	// One line between every two file pixels, none on the picture's edges.
	const ImRect placed = screen.Placed();
	const int interiorLines = static_cast<int>(kTinyPicture.x) - 1 + static_cast<int>(kTinyPicture.y) - 1;
	CHECK(VerticesInColour(CanvasScreen::DrawList(), kGridColour) == interiorLines * 4);
	CHECK(HasVertexAt(CanvasScreen::DrawList(), kGridColour, ImVec2(placed.Min.x + kGridZoom, placed.Min.y)));
	CHECK(HasVertexAt(CanvasScreen::DrawList(), kGridColour, ImVec2(placed.Min.x, placed.Min.y + kGridZoom)));

	screen.ShowAt(kStepBelowTheGrid);

	CHECK_FALSE(DrawsInColour(CanvasScreen::DrawList(), kGridColour));

	screen.Input.PixelGrid = false;
	screen.ShowAt(kGridZoom);

	CHECK_FALSE(DrawsInColour(CanvasScreen::DrawList(), kGridColour));
}

TEST_CASE("Only the grid lines inside the canvas are drawn")
{
	CanvasScreen screen(kLargePicture);

	screen.ShowAt(kGridZoom);

	const ImVec2 area = screen.Result.Area.GetSize();
	const int linesAcross = static_cast<int>(std::ceil(area.x / kGridZoom)) + 1;
	const int linesDown = static_cast<int>(std::ceil(area.y / kGridZoom)) + 1;
	const int drawn = VerticesInColour(CanvasScreen::DrawList(), kGridColour);
	CHECK(drawn > 0);
	CHECK(drawn <= (linesAcross + linesDown) * 4);
}

TEST_CASE("The canvas covers the window's inner rectangle from the cursor down without growing scrollbars")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Idle(3);

	const ImGuiWindow* window = ImGui::FindWindowByName(kWindow);
	REQUIRE(window != nullptr);

	CHECK(screen.Result.Area.Min.x == window->InnerRect.Min.x);
	CHECK(screen.Result.Area.Max.x == window->InnerRect.Max.x);
	CHECK(screen.Result.Area.Min.y == ImGui::GetStyle().WindowPadding.y);
	CHECK(screen.Result.Area.Max.y == window->InnerRect.Max.y);
	CHECK(screen.Result.Area.Min.x < ImGui::GetStyle().WindowPadding.x);
	CHECK_FALSE(window->ScrollbarX);
	CHECK_FALSE(window->ScrollbarY);
}

TEST_CASE("The canvas reports the pixel under the pointer")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Draw();

	const ImRect placed = screen.Placed();
	screen.MoveTo(ImVec2(placed.Min.x + 10.5f, placed.Min.y + 7.5f));

	REQUIRE(screen.Result.Hovered.has_value());
	CHECK(*screen.Result.Hovered == PixelPoint{10, 7});
	CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_Hand);

	screen.MoveTo(ImVec2(placed.Min.x - 5.0f, placed.Min.y - 5.0f));

	CHECK_FALSE(screen.Result.Hovered.has_value());

	screen.MoveTo(kFarAway);

	CHECK_FALSE(screen.Result.Hovered.has_value());
}

TEST_CASE("Quarter wheel notches zoom one step after four")
{
	CanvasScreen screen(kFitsAtFullSize);
	screen.Draw();
	screen.MoveTo(screen.Placed().GetCenter());

	for (int quarter = 0; quarter < 3; ++quarter)
	{
		ImGui::GetIO().AddMouseWheelEvent(0.0f, 0.25f);
		screen.Draw();
		CHECK(screen.Zoom.Zoom == doctest::Approx(1.0f));
	}

	ImGui::GetIO().AddMouseWheelEvent(0.0f, 0.25f);
	screen.Draw();

	CHECK(screen.Zoom.Zoom == doctest::Approx(1.5f));
}

TEST_CASE("At 150% interface scale the fit margin is 36 px")
{
	CanvasScreen screen(kTooLargeToFit);
	ImGui::GetStyle().FontScaleMain = 1.5f;

	screen.Draw();

	const ImVec2 area = screen.Result.Area.GetSize();
	const float expected = std::min((area.x - 72.0f) / kTooLargeToFit.x, (area.y - 72.0f) / kTooLargeToFit.y);

	CHECK(Graphite::FitMargin() == doctest::Approx(36.0f));
	CHECK(screen.Zoom.Zoom == doctest::Approx(expected));
}

TEST_CASE("At 150% interface scale the pan keeps 48 px of the picture in view")
{
	CanvasScreen screen(kFitsAtFullSize);
	ImGui::GetStyle().FontScaleMain = 1.5f;
	screen.Draw();

	const ImVec2 start = screen.Placed().GetCenter();
	screen.MoveTo(start);
	screen.Press(true);
	screen.MoveTo(ImVec2(start.x + 900.0f, start.y));
	screen.Press(false);

	CHECK(screen.Placed().Min.x == doctest::Approx(screen.Result.Area.Max.x - 48.0f));
}
