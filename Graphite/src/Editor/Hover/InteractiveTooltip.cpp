#include "Editor/Hover/InteractiveTooltip.h"

#include "Editor/Hover/HoverLayout.h"

#include "Core/App/Redraw.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr float kBothSides = 2.0f;
		constexpr ImGuiID kNoWindowId = 0;
		constexpr int kPushedStyleVars = 3;

		ImGuiViewport* ViewportOf(const InteractiveTooltipPlace& place)
		{
			return place.Viewport != nullptr ? place.Viewport : ImGui::GetMainViewport();
		}
	}

	bool InteractiveTooltipState::Contains(ImVec2 point) const
	{
		return Shown && ImRect(Min, Max).Contains(point);
	}

	TooltipBounds InteractiveTooltipBounds(const InteractiveTooltipPlace& place)
	{
		if (place.BoundsMax.x > place.BoundsMin.x && place.BoundsMax.y > place.BoundsMin.y)
		{
			return TooltipBounds{.Min = place.BoundsMin, .Max = place.BoundsMax};
		}

		const ImGuiViewport* const viewport = ViewportOf(place);

		return TooltipBounds{.Min = viewport->WorkPos,
							 .Max = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x,
										   viewport->WorkPos.y + viewport->WorkSize.y)};
	}

	bool BeginInteractiveTooltip(const char* name, InteractiveTooltipState& state, const InteractiveTooltipPlace& place)
	{
		// WARNING: No tooltip while a modal is up. The tooltip is raised to the front every
		// frame, which would put it over the dialog meant to be the only thing taking input.
		if (ImGui::GetTopMostPopupModal() != nullptr)
		{
			state.Shown = false;
			return false;
		}

		const ImGuiStyle& style = ImGui::GetStyle();

		const ImVec2 lastSize(state.Max.x - state.Min.x, state.Max.y - state.Min.y);

		ImGuiViewport* const viewport = ViewportOf(place);
		const TooltipBounds bounds = InteractiveTooltipBounds(place);

		const float maxWidth = bounds.Max.x - bounds.Min.x;

		const TooltipPlacement placement = PlaceTooltip(place.AnchorMin, place.AnchorMax,
														ImVec2(lastSize.x, state.NaturalHeight), bounds.Min, bounds.Max);

		const float maxHeight = std::max(placement.MaxHeight, place.LeastHeight);

		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowPos(placement.Position, ImGuiCond_Always, placement.Pivot);
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
											ImVec2(maxWidth, maxHeight));
		ImGui::SetNextWindowBgAlpha(1.0f);

		if (!state.Shown)
		{
			ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
		}

		// WARNING: A window rather than BeginTooltip, because a tooltip can be neither hovered nor
		// scrolled. It is dressed in the tooltip's colours and border to read as one.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.WindowPadding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

		constexpr ImGuiWindowFlags kScrollbarsOnlyWindowFlags =
			ImGuiWindowFlags_HorizontalScrollbar |
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove;

		ImGui::Begin(name, nullptr, kScrollbarsOnlyWindowFlags);

		return true;
	}

	void EndInteractiveTooltip(InteractiveTooltipState& state, ImGuiWindow* returnFocusTo)
	{
		const ImGuiStyle& style = ImGui::GetStyle();

		ImGuiWindow* const window = ImGui::GetCurrentWindow();

		const float naturalHeight = window->ContentSizeIdeal.y + style.WindowPadding.y * kBothSides;

		// WARNING: A click inside the tooltip focuses it, and the editor would lose the keyboard
		// to a box that goes away when the mouse leaves. Focus is handed back to the window that
		// held it before the click, and only once no button is down: during a press it would end
		// the drag of a scrollbar. ImGui moves focus on a click at the end of the frame, so every
		// frame the tooltip is not focused, the focused window is the one to return to. It runs
		// after the content: FocusWindow clears the active item, and a button inside released this
		// frame would never report its click.
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (!focused)
		{
			ImGuiWindow* const holder = ImGui::GetCurrentContext()->NavWindow;
			state.FocusBefore = holder != nullptr ? holder->ID : kNoWindowId;
		}
		else if (!ImGui::IsAnyMouseDown())
		{
			ImGuiWindow* const holder = ImGui::FindWindowByID(state.FocusBefore);
			ImGuiWindow* const target =
				holder != nullptr && holder->WasActive ? holder : returnFocusTo;

			if (target != nullptr)
			{
				ImGui::FocusWindow(target);
			}
		}

		// WARNING: A window never focused falls behind whatever is focused after it, so the first
		// click into the editor would bury the tooltip for good. Raising it every frame moves no
		// focus. It follows the hand-back, which raises the window focus went to.
		ImGui::BringWindowToDisplayFront(window);

		const ImVec2 windowPos = ImGui::GetWindowPos();
		const ImVec2 windowSize = ImGui::GetWindowSize();

		ImGui::End();

		ImGui::PopStyleColor();
		ImGui::PopStyleVar(kPushedStyleVars);

		const ImVec2 lastSize(state.Max.x - state.Min.x, state.Max.y - state.Min.y);

		if (!state.Shown || windowSize.x != lastSize.x || windowSize.y != lastSize.y ||
			naturalHeight != state.NaturalHeight)
		{
			Core::Redraw::Request();
		}

		state.NaturalHeight = naturalHeight;

		state.Min = windowPos;
		state.Max = ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
		state.Shown = true;
	}
}
