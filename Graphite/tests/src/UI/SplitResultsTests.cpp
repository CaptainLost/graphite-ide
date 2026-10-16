#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "UI/SplitResults.h"

#include <imgui.h>

using Graphite::DefaultResultsHeight;

namespace {
	struct HeadingLine
	{
		float Height = 0.0f;
		float TextLineHeight = 0.0f;
		float CloseRight = 0.0f;
		float WindowRight = 0.0f;
	};

	HeadingLine DrawHeadingWithClose(Core::IconButtonFit fit)
	{
		ImGuiFrames frames;
		HeadingLine line;

		frames.Next([&] {
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 200.0f));
			ImGui::Begin("Pane", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			const float top = ImGui::GetCursorPosY();
			ImGui::TextUnformatted("Turn 3");
			Graphite::DrawPaneClose(fit);
			line.CloseRight = ImGui::GetItemRectMax().x;
			line.Height = ImGui::GetCursorPosY() - top;
			line.TextLineHeight = ImGui::GetTextLineHeightWithSpacing();
			line.WindowRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;

			ImGui::End();
		});

		return line;
	}
}

TEST_CASE("The results get 65 percent of what is left after the divider")
{
	CHECK(DefaultResultsHeight(300.0f, 20.0f) == doctest::Approx(182.0f));
	CHECK(DefaultResultsHeight(120.0f, 0.0f) == doctest::Approx(78.0f));
}

TEST_CASE("A pane's close button after a heading of plain text keeps that heading one text line high")
{
	const HeadingLine line = DrawHeadingWithClose(Core::IconButtonFit::Line);

	CHECK(line.Height == doctest::Approx(line.TextLineHeight));
}

TEST_CASE("A pane's close button ends at the pane's padding, whichever fit it has")
{
	for (const Core::IconButtonFit fit : {Core::IconButtonFit::Line, Core::IconButtonFit::Frame})
	{
		const HeadingLine line = DrawHeadingWithClose(fit);

		CHECK(line.CloseRight == doctest::Approx(line.WindowRight));
	}
}

TEST_CASE("A panel shorter than its divider gives the results nothing rather than a negative height")
{
	CHECK(DefaultResultsHeight(10.0f, 20.0f) == 0.0f);
}
