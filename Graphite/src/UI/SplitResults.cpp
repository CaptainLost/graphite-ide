#include "UI/SplitResults.h"

#include "Core/ImGui/IconButton.h"

#include <IconsLucide.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Graphite {

	bool DrawPaneClose(Core::IconButtonFit fit)
	{
		constexpr bool kNotEngaged = false;

		const float cornerX = ImGui::GetWindowWidth() - Core::IconButtonSize(fit) - ImGui::GetStyle().WindowPadding.x;
		const float x = std::max(cornerX, ImGui::GetCursorPosX());

		ImGui::SameLine(x);

		return Core::DrawIconButton("##paneClose", ICON_LC_X, kNotEngaged, "Close", {.Fit = fit});
	}

	float PaneCloseRoom(Core::IconButtonFit fit)
	{
		return Core::IconButtonSize(fit) + ImGui::GetStyle().ItemSpacing.x;
	}

	float DefaultResultsHeight(float available, float divider)
	{
		return std::max(0.0f, available - divider) * kDefaultResultsShare;
	}

	void DrawSplitResults(const SplitResultsInput& input)
	{
		if (!input.HasSelection)
		{
			if (ImGui::BeginChild(input.ResultsChildId, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
								  ImGuiWindowFlags_HorizontalScrollbar))
			{
				input.DrawResults();
			}

			ImGui::EndChild();
			return;
		}

		const float available = ImGui::GetContentRegionAvail().y;

		const float thickness = std::max(1.0f, ImGui::GetStyle().DockingSeparatorSize);

		constexpr float kSpacingsAroundDivider = 2.0f;
		constexpr float kTightestShareOfPanel = 0.25f;
		constexpr float kRowsPerPane = 2.0f;

		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float divider = thickness + spacing * kSpacingsAroundDivider;

		const float minimum = std::min(ImGui::GetFrameHeight() * kRowsPerPane, available * kTightestShareOfPanel);

		if (input.ResultsHeight <= 0.0f)
		{
			input.ResultsHeight = DefaultResultsHeight(available, divider);
		}

		float previewHeight = available - divider - input.ResultsHeight;

		if (previewHeight < minimum)
		{
			previewHeight = minimum;
			input.ResultsHeight = available - divider - previewHeight;
		}

		if (input.ResultsHeight < minimum)
		{
			input.ResultsHeight = minimum;
			previewHeight = available - divider - input.ResultsHeight;
		}

		if (ImGui::BeginChild(input.ResultsChildId, ImVec2(0.0f, input.ResultsHeight),
							  ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			input.DrawResults();
		}

		ImGui::EndChild();

		// WARNING: copies the 0.04s ImGui waits before highlighting a window's resize border
		// (WINDOWS_RESIZE_FROM_EDGES_FEEDBACK_TIMER in imgui.cpp), which is file-static.
		constexpr float kSplitterHoverDelay = 0.04f;

		const ImVec2 barTopLeft = ImGui::GetCursorScreenPos();
		const ImRect splitterBar(barTopLeft, ImVec2(barTopLeft.x + ImGui::GetContentRegionAvail().x, barTopLeft.y + thickness));

		ImGui::SplitterBehavior(splitterBar, ImGui::GetID(input.SplitterId), ImGuiAxis_Y,
								&input.ResultsHeight, &previewHeight, minimum, minimum,
								ImGui::GetCurrentContext()->WindowsBorderHoverPadding,
								kSplitterHoverDelay);

		// WARNING: SplitterBehavior never calls ItemSize, so the cursor is still where the bar
		// starts. A Dummy would leave the bar's height in the layout for the next item to trip on.
		ImGui::SetCursorScreenPos(ImVec2(barTopLeft.x, barTopLeft.y + thickness + spacing));

		// WARNING: ImGui zeroes a child's padding when its border size resolves to zero, which
		// clips the preview's first line against the divider; AlwaysUseWindowPadding prevents it.
		if (ImGui::BeginChild(input.PreviewChildId, ImVec2(0.0f, 0.0f),
							  ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			input.DrawPreview();
		}

		ImGui::EndChild();
	}
}
