#include <doctest/doctest.h>

#include "Search/PreviewContent.h"
#include "Search/SearchPreview.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/Palettes.h"

#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/DocumentSet.h"

#include "ImGuiFrames.h"
#include "TempDirectory.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <vector>

namespace {

	constexpr const char* kPreviewWindow = "Preview";
	constexpr float kFontSize = 13.0f;

	float LeftmostDrawnIn(const ImDrawList& drawing, ImU32 colour)
	{
		float leftmost = FLT_MAX;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col == colour)
			{
				leftmost = std::min(leftmost, vertex.pos.x);
			}
		}

		return leftmost;
	}
}

TEST_CASE("The preview draws text after a two-byte character in the cell its match highlight starts at")
{
	TempDirectory temp;
	const std::filesystem::path file = temp.MakeFile("a.cpp", "\"\xC3\xA9\"; int x;\n");

	Core::DocumentSet documents;
	Graphite::PreviewContent content;
	content.Show(file, documents);
	REQUIRE(content.GetState() == Graphite::PreviewState::Ready);

	const std::size_t intFrom = 6;
	const std::size_t intTo = 9;
	const std::vector<Graphite::SearchHit> hits{
		Graphite::SearchHit{.File = file, .Line = 0, .Text = "int", .From = intFrom, .To = intTo}};

	const Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
	float cellWidth = 0.0f;

	ImGuiFrames frames;
	frames.Next([&] {
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(600.0f, 400.0f));
		ImGui::Begin(kPreviewWindow);
		Graphite::DrawSearchPreview(content, {.Hits = hits, .Current = 0, .Scheme = scheme, .FontSize = kFontSize});

		ImGui::PushFont(nullptr, kFontSize);
		cellWidth = Core::MonospaceCellWidth();
		ImGui::PopFont();

		ImGui::End();
	});

	const ImGuiWindow* window = ImGui::FindWindowByName(kPreviewWindow);
	REQUIRE(window != nullptr);

	const ImU32 keywordColour = Graphite::ReadSyntaxPalette(scheme).Of(Graphite::TokenKind::Keyword);
	const ImU32 highlightColour = Graphite::ReadEditorPalette(scheme).FindCurrentMatch;

	const float keywordLeft = LeftmostDrawnIn(*window->DrawList, keywordColour);
	const float highlightLeft = LeftmostDrawnIn(*window->DrawList, highlightColour);
	REQUIRE(keywordLeft < FLT_MAX);
	REQUIRE(highlightLeft < FLT_MAX);
	REQUIRE(cellWidth > 0.0f);

	CHECK(std::fabs(keywordLeft - highlightLeft) < cellWidth * 0.5f);
}
