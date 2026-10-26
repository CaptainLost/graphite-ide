#include <doctest/doctest.h>

#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"

#include "Editor/Breadcrumb.h"
#include "Image/ImageTools.h"

#include "Core/ImGui/FontSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <functional>
#include <vector>

using Graphite::ImageToolAction;
using Graphite::ImageToolResult;
using Graphite::PictureBackground;

namespace {

	constexpr ImVec2 kWindowSize(600.0f, 60.0f);
	constexpr float kRowMiddle = 12.0f;
	constexpr float kHalf = 0.5f;
	constexpr const char* kWindow = "Tools";
	constexpr std::size_t kZoomOutIndex = 0;
	constexpr std::size_t kMenuIndex = 1;
	constexpr std::size_t kZoomInIndex = 2;
	constexpr std::size_t kFitIndex = 3;
	constexpr std::size_t kActualSizeIndex = 4;
	constexpr std::size_t kCheckerboardIndex = 5;
	constexpr std::size_t kDarkIndex = 6;
	constexpr std::size_t kLightIndex = 7;
	constexpr std::size_t kButtonCount = 8;

	struct ToolsScreen
	{
		float Zoom = 1.0f;
		PictureBackground Background = PictureBackground::Checkerboard;
		ImageToolResult Last;
		float LastItemRight = 0.0f;
		std::vector<ImVec2> Glyphs;

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(kWindowSize);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
			ImGui::Begin(kWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PopStyleVar();

			ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
			const ImageToolResult result = Graphite::DrawImageTools(Zoom, Background);

			if (result.Action != ImageToolAction::None)
			{
				Last = result;
			}

			LastItemRight = ImGui::GetItemRectMax().x;

			Glyphs.clear();
			for (const DrawnGlyph& glyph : DrawnGlyphs(*ImGui::GetWindowDrawList()))
			{
				Glyphs.push_back(glyph.Uv);
			}

			ImGui::End();
		}

		std::function<void()> Drawer()
		{
			return [this] { Draw(); };
		}
	};

	// The buttons are found by moving the pointer along the row, as ImGui reports the item under it.
	std::vector<float> ButtonCentres(ImGuiFrames& frames, ToolsScreen& screen)
	{
		std::vector<float> centres;
		ImGuiID inside = 0;
		float start = 0.0f;

		for (float x = 0.0f; x < kWindowSize.x; x += 1.0f)
		{
			ImGui::GetIO().AddMousePosEvent(x, kRowMiddle);
			frames.Next(screen.Drawer());
			const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;

			if (hovered != 0 && inside == 0)
			{
				start = x;
			}
			else if (hovered == 0 && inside != 0)
			{
				centres.push_back((start + x) * kHalf);
			}

			inside = hovered;
		}

		return centres;
	}

	struct Clicked
	{
		ImageToolResult Result;
		bool Found = false;
	};

	Clicked ClickButton(std::size_t index)
	{
		ImGuiFrames frames;
		ToolsScreen screen;
		frames.Next(screen.Drawer());

		const std::vector<float> centres = ButtonCentres(frames, screen);

		if (centres.size() != kButtonCount)
		{
			return {};
		}

		screen.Last = {};
		frames.Click(ImVec2(centres[index], kRowMiddle), screen.Drawer());
		return {screen.Last, true};
	}
}

TEST_CASE("The image tools zoom in, out, to Fit and to 100%")
{
	const Clicked zoomOut = ClickButton(kZoomOutIndex);
	REQUIRE(zoomOut.Found);
	CHECK(zoomOut.Result.Action == ImageToolAction::ZoomOut);

	CHECK(ClickButton(kZoomInIndex).Result.Action == ImageToolAction::ZoomIn);
	CHECK(ClickButton(kFitIndex).Result.Action == ImageToolAction::Fit);
	CHECK(ClickButton(kActualSizeIndex).Result.Action == ImageToolAction::ActualSize);
}

TEST_CASE("The zoom menu sets the chosen zoom")
{
	ImGuiFrames frames;
	ToolsScreen screen;
	frames.Next(screen.Drawer());

	const std::vector<float> centres = ButtonCentres(frames, screen);
	REQUIRE(centres.size() == kButtonCount);

	frames.Click(ImVec2(centres[kMenuIndex], kRowMiddle), screen.Drawer());
	REQUIRE(ImGui::GetCurrentContext()->OpenPopupStack.Size == 1);

	screen.Last = {};
	REQUIRE(frames.ClickMenuItem("200%", screen.Drawer()));

	CHECK(screen.Last.Action == ImageToolAction::SetZoom);
	CHECK(screen.Last.Zoom == doctest::Approx(2.0f));
}

TEST_CASE("The zoom menu fits the picture from its first entry")
{
	ImGuiFrames frames;
	ToolsScreen screen;
	frames.Next(screen.Drawer());

	const std::vector<float> centres = ButtonCentres(frames, screen);
	REQUIRE(centres.size() == kButtonCount);

	frames.Click(ImVec2(centres[kMenuIndex], kRowMiddle), screen.Drawer());

	screen.Last = {};
	REQUIRE(frames.ClickMenuItem("Fit", screen.Drawer()));
	CHECK(screen.Last.Action == ImageToolAction::Fit);
}

TEST_CASE("The zoom label shows the current zoom")
{
	ImGuiFrames frames;
	ToolsScreen screen;
	screen.Zoom = 1.5f;
	frames.Next(screen.Drawer());
	frames.Next(screen.Drawer());

	// Drawn twice: the first frame bakes the glyphs, and the atlas may move them before the second.
	std::vector<ImVec2> label;
	const auto drawReference = [&] {
		label.clear();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 100.0f));
		ImGui::Begin("Reference", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
		Core::PushCodeFont(Graphite::kBreadcrumbFontSize);
		ImGui::TextUnformatted("150%");
		ImGui::PopFont();

		for (const DrawnGlyph& glyph : DrawnGlyphs(*ImGui::GetWindowDrawList()))
		{
			label.push_back(glyph.Uv);
		}

		ImGui::End();
	};
	frames.Next(drawReference);
	frames.Next(screen.Drawer());
	frames.Next(drawReference);
	REQUIRE(label.size() == 4);

	const auto found = std::search(screen.Glyphs.begin(), screen.Glyphs.end(), label.begin(), label.end(),
								   [](const ImVec2& a, const ImVec2& b) { return a.x == b.x && a.y == b.y; });
	CHECK(found != screen.Glyphs.end());
}

TEST_CASE("A swatch sets the background")
{
	const Clicked dark = ClickButton(kDarkIndex);
	REQUIRE(dark.Found);
	CHECK(dark.Result.Action == ImageToolAction::SetBackground);
	CHECK(dark.Result.Background == PictureBackground::Dark);

	const Clicked light = ClickButton(kLightIndex);
	CHECK(light.Result.Action == ImageToolAction::SetBackground);
	CHECK(light.Result.Background == PictureBackground::Light);

	const Clicked checkerboard = ClickButton(kCheckerboardIndex);
	CHECK(checkerboard.Result.Action == ImageToolAction::SetBackground);
	CHECK(checkerboard.Result.Background == PictureBackground::Checkerboard);
}

TEST_CASE("The image tools' frames fit inside the path row")
{
	ImGuiFrames frames;
	ToolsScreen screen;
	frames.Next(screen.Drawer());

	frames.Next([&] {
		const float height = Graphite::ImageToolsHeight();

		CHECK(height > 0.0f);
		CHECK(height <= Graphite::BreadcrumbHeight());
	});

	// Nothing is hovered along the row's last line, which the tools' frames leave free.
	const float belowTheFrames = Graphite::BreadcrumbHeight() - 1.0f;

	for (float x = 0.0f; x < kWindowSize.x; x += 1.0f)
	{
		ImGui::GetIO().AddMousePosEvent(x, belowTheFrames);
		frames.Next(screen.Drawer());
		CHECK(ImGui::GetCurrentContext()->HoveredId == 0);
	}
}

TEST_CASE("The image tools take the width they report")
{
	ImGuiFrames frames;
	ToolsScreen screen;
	frames.Next(screen.Drawer());
	frames.Next(screen.Drawer());

	frames.Next([&] {
		const float reported = Graphite::ImageToolsWidth();

		CHECK(reported > 0.0f);
		CHECK(screen.LastItemRight == doctest::Approx(reported).epsilon(0.001));
	});
}
