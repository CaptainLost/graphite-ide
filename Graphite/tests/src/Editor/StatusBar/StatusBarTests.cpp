#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"

#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <IconsMaterialDesignIcons.h>

#include <filesystem>
#include <optional>

namespace {
	struct StatusBarScreen
	{
		ImGuiFrames Frames;
		Core::DocumentSet Documents;
		Graphite::StatusMessages Messages;
		bool BellClicked = false;
		Graphite::StatusBarClick LastClick = Graphite::StatusBarClick::None;

		void Draw()
		{
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

		ImVec2 BellCentre() const
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName("##StatusBar");
			REQUIRE(bar != nullptr);
			const float bellRight = bar->Pos.x + bar->Size.x - bar->WindowPadding.x;
			return ImVec2(bellRight - 3.0f, bar->Pos.y + bar->Size.y * 0.5f);
		}

		static const ImDrawList& BarDrawing()
		{
			const ImGuiWindow* bar = ImGui::FindWindowByName("##StatusBar");
			REQUIRE(bar != nullptr);
			return *bar->DrawList;
		}
	};

	bool DrawsInColour(const ImDrawList& drawing, ImU32 colour)
	{
		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour)
			{
				return true;
			}
		}
		return false;
	}
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

TEST_CASE("The bell is drawn in the dim text colour at rest and in the text colour under the pointer")
{
	StatusBarScreen screen;
	screen.Frames.Next([&] { screen.Draw(); });
	screen.Frames.Next([&] { screen.Draw(); });
	const ImU32 dimColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), dimColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));

	const ImVec2 bell = screen.BellCentre();
	ImGui::GetIO().AddMousePosEvent(bell.x, bell.y);
	screen.Frames.Next([&] { screen.Draw(); });

	CHECK(DrawsInColour(StatusBarScreen::BarDrawing(), textColour));
	CHECK_FALSE(DrawsInColour(StatusBarScreen::BarDrawing(), dimColour));
}

TEST_CASE("A click on the server's words reports the server when they open its settings")
{
	StatusBarScreen screen;
	screen.Messages.Server = "C#: Roslyn ready";
	screen.Messages.ServerOpensSettings = true;

	float wordsRight = 0.0f;
	float wordsWidth = 0.0f;
	float barMiddle = 0.0f;

	const auto drawAndMeasure = [&] {
		screen.Draw();
		const ImGuiWindow* bar = ImGui::FindWindowByName("##StatusBar");
		REQUIRE(bar != nullptr);
		wordsRight = bar->Pos.x + bar->Size.x - bar->WindowPadding.x - ImGui::CalcTextSize(ICON_MDI_BELL_OUTLINE).x - ImGui::GetFontSize();
		wordsWidth = ImGui::CalcTextSize("C#: Roslyn ready").x;
		barMiddle = bar->Pos.y + bar->Size.y * 0.5f;
	};

	screen.Frames.Next(drawAndMeasure);
	screen.Frames.Click(ImVec2(wordsRight - wordsWidth * 0.5f, barMiddle), drawAndMeasure);

	CHECK(screen.LastClick == Graphite::StatusBarClick::Server);
}

TEST_CASE("Server words that open nothing take no click")
{
	StatusBarScreen screen;
	screen.Messages.Server = "Restricted mode";

	float wordsRight = 0.0f;
	float wordsWidth = 0.0f;
	float barMiddle = 0.0f;

	const auto drawAndMeasure = [&] {
		screen.Draw();
		const ImGuiWindow* bar = ImGui::FindWindowByName("##StatusBar");
		REQUIRE(bar != nullptr);
		wordsRight = bar->Pos.x + bar->Size.x - bar->WindowPadding.x - ImGui::CalcTextSize(ICON_MDI_BELL_OUTLINE).x - ImGui::GetFontSize();
		wordsWidth = ImGui::CalcTextSize("Restricted mode").x;
		barMiddle = bar->Pos.y + bar->Size.y * 0.5f;
	};

	screen.Frames.Next(drawAndMeasure);
	screen.Frames.Click(ImVec2(wordsRight - wordsWidth * 0.5f, barMiddle), drawAndMeasure);

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
