#include <doctest/doctest.h>

#include <imgui.h>
#include <implot.h>

TEST_CASE("A stacked bar chart draws inside an ImGui frame")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImPlotContext* plots = ImPlot::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.DeltaTime = 1.0f / 60.0f;
	io.Fonts->Build();

	const char* labels[] = {"input", "output"};
	const double values[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};

	bool plotted = false;

	ImGui::NewFrame();
	ImGui::Begin("plot");

	if (ImPlot::BeginPlot("##cost"))
	{
		ImPlot::PlotBarGroups(labels, values, 2, 3, 0.67, 0, ImPlotBarGroupsFlags_Stacked);
		ImPlot::EndPlot();
		plotted = true;
	}

	ImGui::End();
	ImGui::EndFrame();

	CHECK(plotted);

	ImPlot::DestroyContext(plots);
	ImGui::DestroyContext(context);
}
