#include "App/TitleBar.h"

#include "App/Logo.h"
#include "App/Version.h"

#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/CaptionLayout.h"
#include "Core/ImGui/Animation.h"
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

		constexpr float kLogoHeightRatio = 0.6f;

		constexpr float kHalf = 0.5f;

		constexpr ImVec4 kCloseHovered{0.75f, 0.16f, 0.16f, 1.0f};

		bool CaptionButton(const char* id, const char* glyph, const Core::CaptionRect& rect,
						   ImU32 hoveredColour, ImU32 heldColour, bool systemOwnsPointer)
		{
			ImGui::SetCursorPos(ImVec2(rect.X, rect.Y));

			const ImVec2 buttonMin = ImGui::GetCursorScreenPos();
			const ImVec2 buttonMax(buttonMin.x + rect.Width, buttonMin.y + rect.Height);

			ImGui::BeginDisabled(systemOwnsPointer);
			const bool pressed = ImGui::InvisibleButton(id, ImVec2(rect.Width, rect.Height));
			const bool held = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();
			// WARNING: Ask for the hover amount every frame, held or not, so the fade out starts from wherever it had got to.
			const float hoverAmount = Core::Animation::HoverAmount(ImGui::GetItemID(), hovered);
			ImGui::EndDisabled();

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			if (held)
			{
				drawList->AddRectFilled(buttonMin, buttonMax, heldColour);
			}
			else if (hoverAmount > 0.0f)
			{
				ImVec4 fadedFill = ImGui::ColorConvertU32ToFloat4(hoveredColour);
				fadedFill.w *= hoverAmount;
				drawList->AddRectFilled(buttonMin, buttonMax, ImGui::ColorConvertFloat4ToU32(fadedFill));
			}

			// WARNING: Floor the position, do not round it. CalcTextSize rounds up, so a glyph centred on a half pixel is resampled and reads blurrier than the menu text beside it.
			const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
			const ImVec2 glyphPosition(std::floor(buttonMin.x + (rect.Width - glyphSize.x) * kHalf),
									   std::floor(buttonMin.y + (rect.Height - glyphSize.y) * kHalf));

			drawList->AddText(glyphPosition, ImGui::GetColorU32(ImGuiCol_Text), glyph);

			return pressed;
		}

		void DrawVersionLabel(const Core::CaptionRect& slot, const std::string& version)
		{
			// WARNING: Do not centre the label on y. BeginMenuBar() already called AlignTextToFramePadding() and that baseline offset survives SetCursorPos, so a second one pushes the label low.
			ImGui::SetCursorPos(ImVec2(slot.X, 0.0f));
			ImGui::TextDisabled("%s", version.c_str());
		}

		Core::CaptionMetrics MeasureDragRegion(float dragBegin, float dragEnd)
		{
			// WARNING: The drag span is passed on whole, never computed from GetMousePos(): once the hit test answers HTCAPTION the pointer belongs to the non-client area and the backend reports an invalid mouse position, which would flip the answer back and forth under a still cursor.
			// WARNING: dragBegin and dragEnd are window-local while the hit test asks in client coordinates. They coincide only because the dockspace host sits at the main viewport's WorkPos, which a bottom-docked status bar leaves equal to the client's top-left.
			Core::CaptionMetrics metrics;
			metrics.Height = Core::Scaled(kCaptionHeight);
			metrics.DragBegin = dragBegin;
			metrics.DragEnd = dragEnd;

			metrics.DragSuppressed = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

			return metrics;
		}

		bool PointerBelongsToSystem(Core::Window& window, const Core::CaptionMetrics& metrics)
		{
			// WARNING: The client size, the point and the maximized flag are not shared with the hit test, only equal to what it sees. They agree because of how the GLFW backend sits on Win32; a framebuffer scaled against the client rectangle would part them.
			int clientWidth = 0;
			int clientHeight = 0;
			window.FramebufferSize(clientWidth, clientHeight);

			const ImVec2 origin = ImGui::GetWindowPos();
			const ImVec2 pointer = ImGui::GetMousePos();

			const Core::WindowState windowState = window.IsMaximized() ? Core::WindowState::Maximized : Core::WindowState::Restored;

			return Core::Classify(metrics, static_cast<float>(clientWidth),
								  static_cast<float>(clientHeight), pointer.x - origin.x,
								  pointer.y - origin.y,
								  windowState) != Core::CaptionHit::Client;
		}

		void DrawWindowButtons(Core::Window& window, const Core::CaptionLayout& layout,
							   bool systemOwnsPointer)
		{
			const ImU32 hoveredColour = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
			const ImU32 heldColour = ImGui::GetColorU32(ImGuiCol_ButtonActive);

			if (CaptionButton("CaptionMinimize", ICON_MDI_WINDOW_MINIMIZE, layout.Minimize,
							  hoveredColour, heldColour, systemOwnsPointer))
			{
				window.Minimize();
			}

			const char* maximizeGlyph =
				window.IsMaximized() ? ICON_MDI_WINDOW_RESTORE : ICON_MDI_WINDOW_MAXIMIZE;

			if (CaptionButton("CaptionMaximize", maximizeGlyph, layout.Maximize, hoveredColour, heldColour,
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

	void TitleBar::RenderLogo(Core::Window& window)
	{
		if (!window.HasCustomTitleBar())
		{
			return;
		}

		const float rowHeight = Core::Scaled(kCaptionHeight);
		const float height = std::round(rowHeight * kLogoHeightRatio);
		const float width = LogoWidth(height);

		const ImVec2 origin(ImGui::GetWindowPos().x + ImGui::GetCursorPosX(), ImGui::GetWindowPos().y);
		DrawLogo(ImGui::GetWindowDrawList(), ImVec2(origin.x + width * kHalf, origin.y + rowHeight * kHalf), height);

		const float halfItemSpacing = ImGui::GetStyle().ItemSpacing.x * kHalf;
		ImGui::Dummy(ImVec2(width + halfItemSpacing, 0.0f));
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

		const float dockspaceHostWidth = ImGui::GetWindowWidth();

		const Core::CaptionLayout layout = Core::LayOutCaption(
			dockspaceHostWidth, Core::Scaled(kCaptionHeight), Core::Scaled(kButtonWidth), versionWidth, Core::Scaled(kVersionGap), menuEnd);

		if (layout.VersionVisible)
		{
			DrawVersionLabel(layout.Version, version);
		}

		const Core::CaptionMetrics metrics = MeasureDragRegion(menuEnd, layout.Minimize.X);

		DrawWindowButtons(window, layout, PointerBelongsToSystem(window, metrics));

		window.SetCaptionMetrics(metrics);
	}
}
