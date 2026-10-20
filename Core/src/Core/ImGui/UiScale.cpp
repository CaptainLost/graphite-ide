#include "corepch.h"

#include "Core/ImGui/UiScale.h"

#include "Core/ImGui/Theme.h"

#include <imgui_internal.h>

#include <algorithm>
#include <iterator>

namespace Core {

	namespace {
		constexpr float kUnchangedFactor = 1.0f;
		constexpr float kHairlineThickness = 1.0f;
		constexpr float kSquareCorners = 0.0f;
		constexpr float kOpaque = 1.0f;

		// WARNING: ScaleAllSizes truncates, so below a scale of 1 a 1px line comes out 0 and is not drawn at all.
		void KeepHairline(float& scaledThickness, float unscaledThickness)
		{
			if (unscaledThickness > 0.0f && scaledThickness < kHairlineThickness)
			{
				scaledThickness = std::min(unscaledThickness, kHairlineThickness);
			}
		}
	}

	// WARNING: only meaningful while a window is being drawn. EndFrame leaves no viewport current, which resets FontScaleDpi to 1, so a call between frames quietly loses the monitor's scale.
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

	float ScaleChangeFactor(float previousScale, float nextScale)
	{
		const bool previousIsNotAScale = previousScale <= 0.0f;
		const bool nextIsNotAScale = nextScale <= 0.0f;

		if (previousIsNotAScale || nextIsNotAScale)
		{
			return kUnchangedFactor;
		}

		return nextScale / previousScale;
	}

	// WARNING: call between frames only, because dock nodes are being laid out while a frame runs.
	void ScaleDockNodeSizes(float factor)
	{
		const bool changesNothing = factor == kUnchangedFactor;
		const bool isNotAScale = factor <= 0.0f;

		if (changesNothing || isNotAScale)
		{
			return;
		}

		ImGuiContext* context = ImGui::GetCurrentContext();

		if (context == nullptr)
		{
			return;
		}

		for (const ImGuiStoragePair& dockNodeEntry : context->DockContext.Nodes.Data)
		{
			ImGuiDockNode* dockNode = static_cast<ImGuiDockNode*>(dockNodeEntry.val_p);

			if (dockNode != nullptr)
			{
				dockNode->SizeRef.x *= factor;
				dockNode->SizeRef.y *= factor;
			}
		}
	}

	ImGuiStyle ScaledStyle(const ImGuiStyle& current, float scale, bool squareViewportWindows)
	{
		ImGuiStyle style;
		Theme::SetMetrics(style, Theme::AppliedMetrics());

		if (squareViewportWindows)
		{
			style.WindowRounding = kSquareCorners;
		}

		const ImGuiStyle unscaledStyle = style;
		style.ScaleAllSizes(scale);

		KeepHairline(style.WindowBorderSize, unscaledStyle.WindowBorderSize);
		KeepHairline(style.ChildBorderSize, unscaledStyle.ChildBorderSize);
		KeepHairline(style.PopupBorderSize, unscaledStyle.PopupBorderSize);
		KeepHairline(style.FrameBorderSize, unscaledStyle.FrameBorderSize);
		KeepHairline(style.ImageBorderSize, unscaledStyle.ImageBorderSize);
		KeepHairline(style.TabBorderSize, unscaledStyle.TabBorderSize);
		KeepHairline(style.TabBarBorderSize, unscaledStyle.TabBarBorderSize);
		KeepHairline(style.TabBarOverlineSize, unscaledStyle.TabBarOverlineSize);
		KeepHairline(style.TreeLinesSize, unscaledStyle.TreeLinesSize);
		KeepHairline(style.DragDropTargetBorderSize, unscaledStyle.DragDropTargetBorderSize);
		KeepHairline(style.InputTextCursorSize, unscaledStyle.InputTextCursorSize);
		KeepHairline(style.SeparatorSize, unscaledStyle.SeparatorSize);
		KeepHairline(style.SeparatorTextBorderSize, unscaledStyle.SeparatorTextBorderSize);
		KeepHairline(style.DockingSeparatorSize, unscaledStyle.DockingSeparatorSize);
		KeepHairline(style.MouseCursorScale, unscaledStyle.MouseCursorScale);

		std::copy(std::begin(current.Colors), std::end(current.Colors), std::begin(style.Colors));

		if (squareViewportWindows)
		{
			style.Colors[ImGuiCol_WindowBg].w = kOpaque;
		}

		style.FontSizeBase = current.FontSizeBase;
		style.FontScaleMain = current.FontScaleMain;
		style.FontScaleDpi = current.FontScaleDpi;

		return style;
	}
}
