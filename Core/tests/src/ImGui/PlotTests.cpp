#include <doctest/doctest.h>

#include <imgui.h>
#include <implot.h>

namespace {
	const ImVec2 kDisplaySize(800.0f, 600.0f);
	constexpr float kFrameSeconds = 1.0f / 60.0f;
	constexpr int kSeriesCount = 2;
	constexpr int kGroupCount = 3;
	constexpr double kBarWidthShare = 0.67;
	constexpr double kNoShift = 0.0;
}

TEST_CASE("A stacked bar chart draws inside an ImGui frame")
{
	ImGuiContext* imguiContext = ImGui::CreateContext();
	ImPlotContext* plotContext = ImPlot::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = kDisplaySize;
	io.DeltaTime = kFrameSeconds;
	io.Fonts->Build();

	const char* labels[] = {"input", "output"};
	const double values[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

	bool plotted = false;

	ImGui::NewFrame();
	ImGui::Begin("plot");

	if (ImPlot::BeginPlot("##cost"))
	{
		ImPlot::PlotBarGroups(labels, values, kSeriesCount, kGroupCount, kBarWidthShare, kNoShift, ImPlotBarGroupsFlags_Stacked);
		ImPlot::EndPlot();
		plotted = true;
	}

	ImGui::End();
	ImGui::EndFrame();

	CHECK(plotted);

	ImPlot::DestroyContext(plotContext);
	ImGui::DestroyContext(imguiContext);
}
