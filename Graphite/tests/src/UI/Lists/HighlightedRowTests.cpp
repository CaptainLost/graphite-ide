#include <doctest/doctest.h>

#include "ImGuiFrames.h"
#include "UI/Lists/HighlightedRow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <string_view>

using Graphite::HighlightedLabel;
using Graphite::SplitHighlight;

namespace {

	constexpr float kWindowWidth = 320.0f;
	constexpr float kWindowHeight = 200.0f;

	struct DrawnRow
	{
		float DrawnWidth = 0.0f;
		float LabelWidth = 0.0f;
		float Advance = 0.0f;
		float RowHeight = 0.0f;
	};

	DrawnRow DrawHighlightedRow(std::string_view label)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kWindowWidth, kWindowHeight));
		ImGui::Begin("Rows", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);

		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const bool notSelected = false;
		const std::string_view noNeedle;

		Graphite::HighlightedSelectable("##row", notSelected, label, noNeedle);

		DrawnRow drawn;
		drawn.DrawnWidth = ImGui::GetCurrentWindow()->DC.CursorMaxPos.x - rowStart.x;
		drawn.LabelWidth = ImGui::CalcTextSize(label.data(), label.data() + label.size()).x;
		drawn.Advance = ImGui::GetCursorScreenPos().y - rowStart.y;
		drawn.RowHeight = ImGui::GetTextLineHeightWithSpacing();

		ImGui::End();
		return drawn;
	}
}

TEST_CASE("A label holding a double hash draws whole and one row high")
{
	ImGuiFrames frames;

	DrawnRow drawn;
	frames.Next([&drawn] { drawn = DrawHighlightedRow("fix##2"); });

	CHECK(drawn.DrawnWidth == doctest::Approx(drawn.LabelWidth));
	CHECK(drawn.Advance == doctest::Approx(drawn.RowHeight));
}

TEST_CASE("A plain label draws whole and one row high")
{
	ImGuiFrames frames;

	DrawnRow drawn;
	frames.Next([&drawn] { drawn = DrawHighlightedRow("fix/#123-release-100%"); });

	CHECK(drawn.DrawnWidth == doctest::Approx(drawn.LabelWidth));
	CHECK(drawn.Advance == doctest::Approx(drawn.RowHeight));
}

TEST_CASE("A label with no needle is drawn whole and unmarked")
{
	const std::string label = "eb694cb  Anna  fix the width";

	const HighlightedLabel split = SplitHighlight(label, "");

	CHECK(std::string(split.Before) == label);
	CHECK(split.Match.empty());
	CHECK(split.After.empty());
}

TEST_CASE("A label the needle does not appear in is drawn whole and unmarked")
{
	const std::string label = "eb694cb  Anna  fix the width";

	const HighlightedLabel split = SplitHighlight(label, "encoding");

	CHECK(std::string(split.Before) == label);
	CHECK(split.Match.empty());
}

TEST_CASE("The needle is cut out of the label where it sits")
{
	const std::string label = "eb694cb  Anna  fix the width";

	const HighlightedLabel split = SplitHighlight(label, "width");

	CHECK(std::string(split.Before) == "eb694cb  Anna  fix the ");
	CHECK(std::string(split.Match) == "width");
	CHECK(split.After.empty());
}

TEST_CASE("The match keeps the label's own case, not the needle's")
{
	const std::string label = "eb694cb  Anna  Fix The Width";

	const HighlightedLabel split = SplitHighlight(label, "fix the");

	CHECK(std::string(split.Match) == "Fix The");
	CHECK(std::string(split.After) == " Width");
}

TEST_CASE("Only the first occurrence is marked, so a row of common words is not mostly highlighted")
{
	const std::string label = "fix the fix";

	const HighlightedLabel split = SplitHighlight(label, "fix");

	CHECK(split.Before.empty());
	CHECK(std::string(split.Match) == "fix");
	CHECK(std::string(split.After) == " the fix");
}

TEST_CASE("Surrounding whitespace is not part of the needle")
{
	const std::string label = "eb694cb  Anna  fix the width";

	const HighlightedLabel split = SplitHighlight(label, "  width ");

	CHECK(std::string(split.Match) == "width");
}

TEST_CASE("A needle of nothing but whitespace marks nothing")
{
	const std::string label = "eb694cb  Anna  fix the width";

	const HighlightedLabel split = SplitHighlight(label, "   ");

	CHECK(std::string(split.Before) == label);
	CHECK(split.Match.empty());
}

TEST_CASE("The three pieces concatenated rebuild the label exactly, so nothing drawn is dropped or invented")
{
	const std::string label = "eb694cb  Anna  fix the width";

	for (const char* needle : {"", "width", "ANNA", "eb694", "nothing", "  "})
	{
		const HighlightedLabel split = SplitHighlight(label, needle);

		CAPTURE(needle);
		CHECK(std::string(split.Before) + std::string(split.Match) + std::string(split.After) == label);
	}
}

TEST_CASE("A pattern the reader typed is marked only where it appears literally, never as the extension it ends with")
{
	const std::string path = "Core/src/Core/App/Application.cpp";

	const HighlightedLabel fragment = SplitHighlight(path, "Application");

	CHECK(std::string(fragment.Match) == "Application");

	const HighlightedLabel glob = SplitHighlight(path, "*.cpp");

	CHECK(glob.Match.empty());
	CHECK(std::string(glob.Before) == path);
}
