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
