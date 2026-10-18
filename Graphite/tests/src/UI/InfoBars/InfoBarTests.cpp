#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "UI/InfoBars/InfoBar.h"

#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/TextMetrics.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>
#include <string>

namespace {
	constexpr float kFringe = 1.0f;
	const ImVec4 kWarningColour(0.9f, 0.7f, 0.1f, 1.0f);

	Graphite::InfoBarContent RestrictedLike()
	{
		return Graphite::InfoBarContent{
			.Id = "##restricted",
			.Weight = Graphite::InfoBarWeight::Warning,
			.Glyph = "!",
			.WeightColour = kWarningColour,
			.Message = "Restricted mode: the language server and background analysis are off for this folder.",
			.Buttons = {{.Label = "Trust Folder", .Primary = true},
						{.Label = "Trust All in D:/Repos/" + std::string(120, 'p')}},
			.CloseTooltip = "Close"};
	}

	struct BarScreen
	{
		ImGuiFrames Frames;
		Graphite::InfoBarContent Content = RestrictedLike();
		float Width = 900.0f;
		Graphite::InfoBarAnswer LastAnswer;
		bool Closed = false;
		std::optional<std::size_t> Pressed;

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(Width, 200.0f));
			ImGui::Begin("Editor area", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			LastAnswer = Graphite::DrawInfoBar(Content);
			Closed = Closed || LastAnswer.Closed;
			if (LastAnswer.Pressed.has_value())
			{
				Pressed = LastAnswer.Pressed;
			}
			ImGui::End();
		}

		void DrawFrame()
		{
			Frames.Next([&] { Draw(); });
		}

		static const ImGuiWindow& Bar()
		{
			const ImGuiWindow* editor = ImGui::FindWindowByName("Editor area");
			REQUIRE(editor != nullptr);
			REQUIRE(!editor->DC.ChildWindows.empty());
			return *editor->DC.ChildWindows.back();
		}

		// How far right anything in `colour` is drawn in the bar.
		static float RightmostInk(ImU32 colour)
		{
			float right = -FLT_MAX;
			for (const ImDrawVert& vertex : Bar().DrawList->VtxBuffer)
			{
				if (vertex.col == colour)
				{
					right = std::max(right, vertex.pos.x);
				}
			}
			return right;
		}
	};
}

TEST_CASE("A warning bar is tinted toward its colour and an information bar keeps the popup surface's")
{
	ImGuiFrames frames;
	frames.Next([] {
		const ImVec4 surface = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
		const ImVec4 warning = Graphite::InfoBarBackground(Graphite::InfoBarWeight::Warning, kWarningColour);
		const ImVec4 information = Graphite::InfoBarBackground(Graphite::InfoBarWeight::Information, kWarningColour);

		CHECK(information.x == doctest::Approx(surface.x));
		CHECK(information.y == doctest::Approx(surface.y));
		CHECK(information.z == doctest::Approx(surface.z));

		const auto distance = [](const ImVec4& from, const ImVec4& to) {
			return std::abs(from.x - to.x) + std::abs(from.y - to.y) + std::abs(from.z - to.z);
		};
		CHECK(distance(warning, kWarningColour) < distance(surface, kWarningColour));
	});
}

TEST_CASE("The bar's glyph is drawn in its weight's colour")
{
	BarScreen screen;
	screen.DrawFrame();

	CHECK(DrawsInColour(*BarScreen::Bar().DrawList, ImGui::GetColorU32(kWarningColour)));
}

TEST_CASE("The primary button is filled with the accent and the others have no fill at rest")
{
	BarScreen screen;
	screen.DrawFrame();

	const ImGuiWindow& bar = BarScreen::Bar();
	CHECK(DrawsInColour(*bar.DrawList, ImGui::GetColorU32(ImGuiCol_CheckMark)));
	CHECK_FALSE(DrawsInColour(*bar.DrawList, ImGui::GetColorU32(ImGuiCol_Button)));
}

TEST_CASE("In a bar too narrow for its buttons they stand clear of the close button, which still closes the bar")
{
	for (const float width : {520.0f, 400.0f, 260.0f})
	{
		CAPTURE(width);

		BarScreen screen;
		screen.Width = width;
		screen.DrawFrame();

		const ImGuiWindow& bar = BarScreen::Bar();
		const float closeLeft =
			bar.InnerRect.Max.x - bar.WindowPadding.x - Core::IconButtonSize() - ImGui::GetStyle().ItemSpacing.x;
		CHECK(BarScreen::RightmostInk(ImGui::GetColorU32(ImGuiCol_CheckMark)) <= closeLeft + kFringe);

		const ImVec2 close(bar.InnerRect.Max.x - bar.WindowPadding.x - Core::IconButtonSize() * 0.5f,
						   bar.InnerRect.GetCenter().y);
		screen.Frames.Click(close, [&] { screen.Draw(); });

		CHECK(screen.Closed);
		CHECK_FALSE(screen.Pressed.has_value());
	}
}

TEST_CASE("A button's label is drawn whole though it holds a double hash, as a folder named C# does")
{
	BarScreen screen;
	screen.Content.Message.clear();
	screen.Content.CloseTooltip = nullptr;
	screen.Content.Buttons = {{.Label = "C##D"}};
	screen.DrawFrame();

	const float labelWidth = Core::TextWidth("C##D");
	const ImGuiWindow& bar = BarScreen::Bar();

	float left = FLT_MAX;
	float right = -FLT_MAX;
	for (const ImDrawVert& vertex : bar.DrawList->VtxBuffer)
	{
		if (vertex.col == ImGui::GetColorU32(ImGuiCol_Text))
		{
			left = std::min(left, vertex.pos.x);
			right = std::max(right, vertex.pos.x);
		}
	}

	REQUIRE(left < FLT_MAX);
	CHECK(right - left > labelWidth * 0.75f);
}

TEST_CASE("A click on the close button reports the bar closed")
{
	BarScreen screen;
	screen.DrawFrame();

	const ImGuiWindow& bar = BarScreen::Bar();
	const ImVec2 close(bar.InnerRect.Max.x - bar.WindowPadding.x - ImGui::GetFrameHeight() * 0.5f,
					   bar.InnerRect.GetCenter().y);
	screen.Frames.Click(close, [&] { screen.Draw(); });

	CHECK(screen.Closed);
	CHECK_FALSE(screen.Pressed.has_value());
}

TEST_CASE("A bar without a close tooltip has no close button")
{
	BarScreen screen;
	screen.Content.CloseTooltip = nullptr;
	screen.Content.Buttons.clear();
	screen.DrawFrame();

	const ImGuiWindow& bar = BarScreen::Bar();
	const ImVec2 whereCloseWouldBe(bar.InnerRect.Max.x - bar.WindowPadding.x - ImGui::GetFrameHeight() * 0.5f,
								   bar.InnerRect.GetCenter().y);
	screen.Frames.Click(whereCloseWouldBe, [&] { screen.Draw(); });

	CHECK_FALSE(screen.Closed);
}

TEST_CASE("A click on a button reports which one")
{
	BarScreen screen;
	screen.Content.Buttons = {{.Label = "Configure", .Primary = true}};
	screen.DrawFrame();

	const ImGuiWindow& bar = BarScreen::Bar();
	const float closeRoom = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth = ImGui::CalcTextSize("Configure").x + ImGui::GetStyle().FramePadding.x * 2.0f;
	const ImVec2 onTheButton(bar.InnerRect.Max.x - bar.WindowPadding.x - closeRoom - buttonWidth * 0.5f,
							 bar.InnerRect.GetCenter().y);
	screen.Frames.Click(onTheButton, [&] { screen.Draw(); });

	REQUIRE(screen.Pressed.has_value());
	CHECK(*screen.Pressed == 0);
	CHECK_FALSE(screen.Closed);
}
