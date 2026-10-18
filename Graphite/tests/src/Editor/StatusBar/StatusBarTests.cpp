#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"

#include "Core/ImGui/Animation.h"
#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <IconsMaterialDesignIcons.h>

#include <filesystem>
#include <optional>

namespace {
	constexpr float kInsideBellInset = 3.0f;

	struct StatusBarScreen
	{
		ImGuiFrames Frames;
		Core::DocumentSet Documents;
		Graphite::StatusMessages Messages;
		bool BellClicked = false;
		float WordsRight = 0.0f;
		float WordsWidth = 0.0f;
		float BarMiddle = 0.0f;
		Graphite::StatusBarClick LastClick = Graphite::StatusBarClick::None;

		void Draw()
		{
			Core::Animation::BeginFrame();
			const Graphite::StatusBarClick clicked = Graphite::DrawStatusBar(Documents, std::nullopt, std::filesystem::path(), Messages);
			if (clicked != Graphite::StatusBarClick::None)
			{
				LastClick = clicked;
			}
			if (clicked == Graphite::StatusBarClick::Notifications)
			{
				BellClicked = true;
			}
		}

		void DrawMeasuringWords(const char* words)
		{
			Draw();
			const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
			REQUIRE(bar != nullptr);
			WordsRight = bar->Pos.x + bar->Size.x - bar->WindowPadding.x - ImGui::CalcTextSize(ICON_MDI_BELL_OUTLINE).x - ImGui::GetFontSize();
			WordsWidth = ImGui::CalcTextSize(words).x;
			BarMiddle = bar->Pos.y + bar->Size.y * 0.5f;
		}

		ImVec2 BellCentre() const
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
			REQUIRE(bar != nullptr);
			const float bellRight = bar->Pos.x + bar->Size.x - bar->WindowPadding.x;
			return ImVec2(bellRight - kInsideBellInset, bar->Pos.y + bar->Size.y * 0.5f);
		}

		static const ImDrawList& BarDrawing()
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
			REQUIRE(bar != nullptr);
			return *bar->DrawList;
		}
	};
}

TEST_CASE("A click on the bell at the status bar's right end reports the notifications")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });

	screen.Frames.Click(screen.BellCentre(), [&] { screen.Draw(); });

	CHECK(screen.BellClicked);
}

TEST_CASE("The bell with a missed notification's dot completes a frame without an ImGui assertion")
{
	StatusBarScreen screen;
	screen.Messages.MissedNotificationColour = Core::Color{.R = 1.0f};
	screen.Messages.Problems = "2 errors";

	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK_FALSE(screen.BellClicked);
}

TEST_CASE("The bell is a glyph button like any other: the text colour at rest, on the hover fill under the pointer")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });
	const ImU32 dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);
	const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), dimColour));

	const ImVec2 bell = screen.BellCentre();
	ImGui::GetIO().AddMousePosEvent(bell.x, bell.y);

	const int framesPastTheHoverFade = 30;
	for (int frame = 0; frame < framesPastTheHoverFade; ++frame)
	{
		screen.Frames.Next([&] { screen.Draw(); });
	}

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), hoverFill));
}

TEST_CASE("A click on the server's words reports the server when they open its settings")
{
	StatusBarScreen screen;
	screen.Messages.Server = "C#: Roslyn ready";
	screen.Messages.ServerOpensSettings = true;

	screen.Frames.Next([&] { screen.DrawMeasuringWords("C#: Roslyn ready"); });
	screen.Frames.Click(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle),
						[&] { screen.DrawMeasuringWords("C#: Roslyn ready"); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::Server);
}

TEST_CASE("Server words that open nothing take no click")
{
	StatusBarScreen screen;
	screen.Messages.Server = "Restricted mode";

	screen.Frames.Next([&] { screen.DrawMeasuringWords("Restricted mode"); });
	screen.Frames.Click(ImVec2(screen.WordsRight - screen.WordsWidth * 0.5f, screen.BarMiddle),
						[&] { screen.DrawMeasuringWords("Restricted mode"); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::None);
}

TEST_CASE("Problems and the background pass beside clickable server words complete a frame without an ImGui assertion")
{
	StatusBarScreen screen;
	screen.Messages.Problems = "2 errors";
	screen.Messages.Server = "C++: clangd indexing 40%";
	screen.Messages.ServerOpensSettings = true;
	screen.Messages.Background = "analysing workspace: 42 of 385";

	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(screen.LastClick == Graphite::StatusBarClick::None);
}

TEST_CASE("The status bar draws no border of its own, so it sits flush with the chrome above it")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });

	const ImGuiWindow* bar = ImGui::FindWindowByName(Graphite::kStatusBarWindowName);
	REQUIRE(bar != nullptr);
	CHECK(bar->WindowBorderSize == doctest::Approx(0.0f));
}
