#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/StatusBar/StatusBar.h"

#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <filesystem>
#include <optional>

namespace {
	struct StatusBarScreen
	{
		ImGuiFrames Frames;
		Core::DocumentSet Documents;
		Graphite::StatusMessages Messages;
		bool BellClicked = false;

		void Draw()
		{
			const Graphite::StatusBarClick clicked = Graphite::DrawStatusBar(Documents, std::nullopt, std::filesystem::path(), Messages);
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
