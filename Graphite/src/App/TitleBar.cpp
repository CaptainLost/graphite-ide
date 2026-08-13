#include "App/TitleBar.h"

#include "App/Version.h"

#include "Core/ImGui/CaptionLayout.h"
#include "Core/Platform/Caption.h"
#include "Core/Platform/Window.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <cmath>
#include <format>

namespace Graphite {

	namespace {
		constexpr float kButtonWidth = 46.0f;
		constexpr float kVersionGap = 12.0f;

		constexpr ImVec4 kCloseHovered{0.75f, 0.16f, 0.16f, 1.0f};

		bool CaptionButton(const char* id, const char* glyph, const Core::CaptionRect& rect,
						   ImU32 hoveredColour, ImU32 heldColour, bool systemOwnsPointer)
		{
			ImGui::SetCursorPos(ImVec2(rect.X, rect.Y));

			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + rect.Width, min.y + rect.Height);

			ImGui::BeginDisabled(systemOwnsPointer);
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(rect.Width, rect.Height));
			const bool held = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();
			ImGui::EndDisabled();

			ImDrawList* draw = ImGui::GetWindowDrawList();

			if (held || hovered)
			{
				draw->AddRectFilled(min, max, held ? heldColour : hoveredColour);
			}

			// Floored, not rounded: CalcTextSize rounds up, so a centred glyph placed on a
			// half pixel would be resampled and read blurrier than the menu text beside it.
			const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
			const ImVec2 at(std::floor(min.x + (rect.Width - glyphSize.x) * 0.5f),
							std::floor(min.y + (rect.Height - glyphSize.y) * 0.5f));

			draw->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), glyph);

			return pressed;
		}

		void DrawVersionLabel(const Core::CaptionRect& slot, const std::string& version)
		{
			// No centring formula on y: BeginMenuBar() already called
			// AlignTextToFramePadding(), and that baseline offset survives SetCursorPos, so a
			// second one on top of it would push the label low.
			ImGui::SetCursorPos(ImVec2(slot.X, 0.0f));
			ImGui::TextDisabled("%s", version.c_str());
		}

		Core::CaptionMetrics MeasureDragRegion(float dragBegin, float dragEnd)
		{
			// The span itself, not "the cursor is over it": the moment the hit test answers
			// HTCAPTION the pointer belongs to the non-client area, GLFW gets WM_MOUSELEAVE
			// and the backend pushes an invalid mouse position, so an answer computed from
			// GetMousePos() would flip the hit test back and forth under a still cursor.
			//
			// These x values are window-local while the hit test asks in client coordinates.
			// They coincide only because the dockspace host sits at the main viewport's
			// WorkPos, which the bottom-docked status bar leaves equal to the client's
			// top-left; a bar docked along the top or the left would need the offset between
			// GetWindowPos() and the viewport's Pos added here.
			Core::CaptionMetrics metrics;
			metrics.Height = kCaptionHeight;
			metrics.DragBegin = dragBegin;
			metrics.DragEnd = dragEnd;

			// An open menu is the one thing a free-caption click has to reach the application
			// for: it should close the menu, and a drag would swallow it. Unlike a hover test
			// this stays true while the pointer is off in the non-client area.
			metrics.DragSuppressed = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

			return metrics;
		}

		bool PointerBelongsToSystem(Core::Window& window, const Core::CaptionMetrics& metrics)
		{
			// The same metrics the hit test is given, so the button highlights follow it
			// rather than guessing alongside it. The client size, the point and the maximized
			// flag are not shared with the frame, only equal: they agree because of how the
			// GLFW backend sits on Win32, and a framebuffer scaled against the client
			// rectangle would part them.
			//
			// An invalid mouse position — what the backend reports while the pointer is away
			// in the non-client area — lands far outside the client rectangle and reads as an
			// edge, which is the wanted answer anyway.
			int clientWidth = 0;
			int clientHeight = 0;
			window.FramebufferSize(clientWidth, clientHeight);

			const ImVec2 origin = ImGui::GetWindowPos();
			const ImVec2 pointer = ImGui::GetMousePos();

			return Core::Classify(metrics, static_cast<float>(clientWidth),
								  static_cast<float>(clientHeight), pointer.x - origin.x,
								  pointer.y - origin.y,
								  window.IsMaximized()) != Core::CaptionHit::Client;
		}

		void DrawWindowButtons(Core::Window& window, const Core::CaptionLayout& layout,
							   bool systemOwnsPointer)
		{
			const ImU32 hovered = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
			const ImU32 held = ImGui::GetColorU32(ImGuiCol_ButtonActive);

			if (CaptionButton("CaptionMinimize", ICON_MDI_WINDOW_MINIMIZE, layout.Minimize,
							  hovered, held, systemOwnsPointer))
			{
				window.Minimize();
			}

			const char* middle =
				window.IsMaximized() ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE;

			if (CaptionButton("CaptionMaximize", middle, layout.Maximize, hovered, held,
							  systemOwnsPointer))
			{
				window.ToggleMaximize();
			}

			const ImU32 closeColour = ImGui::GetColorU32(kCloseHovered);

			if (CaptionButton("CaptionClose", ICON_MDI_CLOSE, layout.Close, closeColour,
							  closeColour, systemOwnsPointer))
			{
				window.RequestClose();
			}
		}
	}

	void TitleBar::Render(Core::Window& window)
	{
		if (!window.HasCustomTitleBar())
		{
			return;
		}

		const std::string version = std::format("Graphite {}", Version::String());
		const float versionWidth = ImGui::CalcTextSize(version.c_str()).x;

		const float menuEnd = ImGui::GetCursorPosX();

		// BeginMenuBar() does not open a child window — it repositions the cursor of the
		// dockspace host window that is already current, so this is that host window's
		// width, which is exactly the width the row spans.
		const float barWidth = ImGui::GetWindowWidth();

		const Core::CaptionLayout layout = Core::LayOutCaption(
			barWidth, kCaptionHeight, kButtonWidth, versionWidth, kVersionGap, menuEnd);

		if (layout.VersionVisible)
		{
			DrawVersionLabel(layout.Version, version);
		}

		const Core::CaptionMetrics metrics = MeasureDragRegion(menuEnd, layout.Minimize.X);

		DrawWindowButtons(window, layout, PointerBelongsToSystem(window, metrics));

		window.SetCaptionMetrics(metrics);
	}
}
