#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Sessions/Charts/StackedBarChart.h"

#include "Core/ImGui/Theme.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

namespace {
	constexpr float kFieldBorder = 1.0f;
	constexpr float kNearTheEdge = 1.5f;

	struct ChartScreen
	{
		ChartScreen() { ImPlot::CreateContext(); }
		~ChartScreen() { ImPlot::DestroyContext(); }

		ChartScreen(const ChartScreen&) = delete;
		ChartScreen& operator=(const ChartScreen&) = delete;

		void Draw()
		{
			Frames.Next([this] {
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(600.0f, 500.0f));
				ImGui::Begin("Chart", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

				if (Graphite::BeginBarChart("Cost", "chart"))
				{
					Graphite::EndBarChart();
					Frame = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				}

				ImGui::End();
			});
		}

		static const ImDrawList& DrawList()
		{
			const ImGuiWindow* window = ImGui::FindWindowByName("Chart");
			REQUIRE(window != nullptr);
			return *window->DrawList;
		}

		ImGuiFrames Frames;
		ImRect Frame;
	};

	bool NearTheEdge(const ImRect& frame, ImVec2 point)
	{
		const ImRect outer(frame.Min.x - kNearTheEdge, frame.Min.y - kNearTheEdge, frame.Max.x + kNearTheEdge,
						   frame.Max.y + kNearTheEdge);
		const ImRect inner(frame.Min.x + kNearTheEdge, frame.Min.y + kNearTheEdge, frame.Max.x - kNearTheEdge,
						   frame.Max.y - kNearTheEdge);

		return outer.Contains(point) && !inner.Contains(point);
	}
}

TEST_CASE("A chart's frame takes the surface colour and no border, though fields have one")
{
	ChartScreen screen;
	Core::Theme::Apply(Core::Theme::DefaultPalette());
	ImGui::GetStyle().FrameBorderSize = kFieldBorder;

	screen.Draw();
	screen.Draw();
	REQUIRE(screen.Frame.GetWidth() > 0.0f);

	const ImU32 line = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 surface = ImGui::GetColorU32(Core::SurfaceColour());
	int lineOnTheEdge = 0;
	int surfaceVertices = 0;

	for (const ImDrawVert& vertex : ChartScreen::DrawList().VtxBuffer)
	{
		lineOnTheEdge += vertex.col == line && NearTheEdge(screen.Frame, vertex.pos) ? 1 : 0;
		surfaceVertices += vertex.col == surface ? 1 : 0;
	}

	CHECK(lineOnTheEdge == 0);
	CHECK(surfaceVertices > 0);
	CHECK(ImGui::GetStyle().FrameBorderSize == doctest::Approx(kFieldBorder));
}
