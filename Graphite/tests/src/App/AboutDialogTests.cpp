#include <doctest/doctest.h>

#include "App/AboutDialog.h"
#include "App/Version.h"
#include "App/VersionNumbers.h"
#include "Core/ImGui/FontSet.h"
#include "UI/Dialogs/ModalGate.h"

#include "ImGuiFrames.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <string_view>

TEST_CASE("The licences text is never empty, whether or not the notices file sits next to the executable")
{
	const std::string notices = Graphite::AboutDialog::LoadNotices();

	CHECK_FALSE(notices.empty());
}

TEST_CASE("The version shown is the one the resource script is built from")
{
	CHECK(std::string(Graphite::Version::String()) ==
		  std::to_string(GRAPHITE_VERSION_MAJOR) + "." + std::to_string(GRAPHITE_VERSION_MINOR) + "." +
			  std::to_string(GRAPHITE_VERSION_PATCH));
}

namespace {
	constexpr float kBaseFontSizeBesideTheNotices = 20.0f;
	constexpr int kFramesToOpenTheDialog = 4;
	constexpr int kVerticesInAGlyph = 4;
	constexpr ImGuiWindowFlags kBareWindow = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
	constexpr const char* kNoticesChildName = "##Licences";

	ImGuiWindow* FindNoticesChild()
	{
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kNoticesChildName) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	bool IsGlyphVertex(const ImDrawVert& vertex)
	{
		const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
		return vertex.uv.x != white.x || vertex.uv.y != white.y;
	}

	float FirstGlyphHeight(const ImDrawList& drawing)
	{
		for (int first = 0; first + kVerticesInAGlyph <= drawing.VtxBuffer.Size; ++first)
		{
			const ImDrawVert* quad = &drawing.VtxBuffer[first];
			const bool isRectangle = quad[0].pos.y == quad[1].pos.y && quad[0].pos.x == quad[3].pos.x;

			if (!IsGlyphVertex(quad[0]) || !isRectangle)
			{
				continue;
			}

			float top = drawing.VtxBuffer[first].pos.y;
			float bottom = top;

			for (int vertex = first + 1; vertex < first + kVerticesInAGlyph; ++vertex)
			{
				top = (std::min)(top, drawing.VtxBuffer[vertex].pos.y);
				bottom = (std::max)(bottom, drawing.VtxBuffer[vertex].pos.y);
			}

			return bottom - top;
		}

		return 0.0f;
	}
}

TEST_CASE("The notices are drawn at the code size the push sets, not at the dialog's size")
{
	ImGuiFrames frames;
	Graphite::ModalGate gate;
	Graphite::AboutDialog dialog{gate};
	ImGui::GetStyle().FontSizeBase = kBaseFontSizeBesideTheNotices;

	dialog.Open();

	const std::string notices = Graphite::AboutDialog::LoadNotices();
	float drawnHeight = 0.0f;
	float referenceHeight = 0.0f;
	float dialogSizeHeight = 0.0f;

	for (int frame = 0; frame < kFramesToOpenTheDialog; ++frame)
	{
		frames.Next([&] {
			dialog.Render();

			const ImGuiWindow* child = FindNoticesChild();
			drawnHeight = child != nullptr ? FirstGlyphHeight(*child->DrawList) : 0.0f;

			ImGui::Begin("reference", nullptr, kBareWindow);
			Core::PushCodeFont(Core::kInterfaceFontSize);
			ImGui::TextUnformatted(notices.c_str(), notices.c_str() + 1);
			ImGui::PopFont();
			referenceHeight = FirstGlyphHeight(*ImGui::GetWindowDrawList());
			ImGui::End();

			ImGui::Begin("dialog size", nullptr, kBareWindow);
			ImGui::TextUnformatted(notices.c_str(), notices.c_str() + 1);
			dialogSizeHeight = FirstGlyphHeight(*ImGui::GetWindowDrawList());
			ImGui::End();
		});
	}

	REQUIRE(drawnHeight > 0.0f);
	CHECK(drawnHeight == doctest::Approx(referenceHeight));
	CHECK(drawnHeight < dialogSizeHeight);
}
