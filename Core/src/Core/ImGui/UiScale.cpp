#include "corepch.h"

#include "Core/ImGui/UiScale.h"

#include "Core/ImGui/Theme.h"

#include <algorithm>
#include <iterator>

namespace Core {

	float UiScale()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		return style.FontScaleMain * style.FontScaleDpi;
	}

	float Scaled(float length)
	{
		return length * UiScale();
	}

	ImVec2 Scaled(ImVec2 size)
	{
		const float scale = UiScale();
		return ImVec2(size.x * scale, size.y * scale);
	}

	ImGuiStyle ScaledStyle(const ImGuiStyle& current, float scale, bool squareViewportWindows)
	{
		ImGuiStyle style;
		Theme::SetMetrics(style);

		if (squareViewportWindows)
		{
			style.WindowRounding = 0.0f;
		}

		const ImGuiStyle unscaled = style;
		style.ScaleAllSizes(scale);

		// ScaleAllSizes truncates, so below 1 a 1px line comes out 0 and is not drawn at all:
		// borders, separators, the caret in every text field. A line that was there stays
		// at least a pixel thick; one the theme switched off stays off.
		const auto keepHairline = [](float& scaled, float before) {
			if (before > 0.0f && scaled < 1.0f)
			{
				scaled = std::min(before, 1.0f);
			}
		};

		keepHairline(style.WindowBorderSize, unscaled.WindowBorderSize);
		keepHairline(style.ChildBorderSize, unscaled.ChildBorderSize);
		keepHairline(style.PopupBorderSize, unscaled.PopupBorderSize);
		keepHairline(style.FrameBorderSize, unscaled.FrameBorderSize);
		keepHairline(style.ImageBorderSize, unscaled.ImageBorderSize);
		keepHairline(style.TabBorderSize, unscaled.TabBorderSize);
		keepHairline(style.TabBarBorderSize, unscaled.TabBarBorderSize);
		keepHairline(style.TabBarOverlineSize, unscaled.TabBarOverlineSize);
		keepHairline(style.TreeLinesSize, unscaled.TreeLinesSize);
		keepHairline(style.DragDropTargetBorderSize, unscaled.DragDropTargetBorderSize);
		keepHairline(style.InputTextCursorSize, unscaled.InputTextCursorSize);
		keepHairline(style.SeparatorSize, unscaled.SeparatorSize);
		keepHairline(style.SeparatorTextBorderSize, unscaled.SeparatorTextBorderSize);
		keepHairline(style.DockingSeparatorSize, unscaled.DockingSeparatorSize);
		keepHairline(style.MouseCursorScale, unscaled.MouseCursorScale);

		std::copy(std::begin(current.Colors), std::end(current.Colors), std::begin(style.Colors));

		if (squareViewportWindows)
		{
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		style.FontSizeBase = current.FontSizeBase;
		style.FontScaleMain = current.FontScaleMain;
		style.FontScaleDpi = current.FontScaleDpi;

		return style;
	}
}
