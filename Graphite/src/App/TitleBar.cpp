#include "App/TitleBar.h"

#include "App/Logo.h"
#include "Core/Base/Color.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/UiScale.h"
#include "Core/ImGui/CaptionLayout.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/Theme.h"
#include "Core/Platform/Caption.h"
#include "Core/Platform/Window.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <optional>
#include <string>

namespace Graphite {

	namespace {
		constexpr float kButtonsGap = 12.0f;
		constexpr float kTitleMargin = 12.0f;
		constexpr float kDividerWidth = 1.0f;
		constexpr float kDividerHeightRatio = 0.5f;
		constexpr float kGroupGap = 16.0f;
		constexpr float kGlyphGap = 6.0f;

		constexpr float kLogoHeightRatio = 0.6f;

		constexpr float kHalf = 0.5f;
		constexpr float kFullyHovered = 1.0f;

		constexpr float kIconGreyShareOfDisabled = 0.21f;

		constexpr ImU32 kCloseHoveredGlyph = IM_COL32(255, 255, 255, 255);

		struct CaptionButtonLook
		{
			ImU32 HoveredFill = 0;
			ImU32 HeldFill = 0;
			ImU32 Glyph = 0;
			ImU32 HoveredGlyph = 0;
		};

		ImU32 BlendColours(ImU32 from, ImU32 to, float amount)
		{
			return ImGui::ColorConvertFloat4ToU32(Core::Animation::Blend(ImGui::ColorConvertU32ToFloat4(from),
																		 ImGui::ColorConvertU32ToFloat4(to), amount));
		}

		bool CaptionButton(const char* id, const char* glyph, const Core::CaptionRect& rect,
						   const CaptionButtonLook& look, bool systemOwnsPointer)
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
				drawList->AddRectFilled(buttonMin, buttonMax, look.HeldFill);
			}
			else if (hoverAmount > 0.0f)
			{
				ImVec4 fadedFill = ImGui::ColorConvertU32ToFloat4(look.HoveredFill);
				fadedFill.w *= hoverAmount;
				drawList->AddRectFilled(buttonMin, buttonMax, ImGui::ColorConvertFloat4ToU32(fadedFill));
			}

			const float glyphHover = held ? kFullyHovered : hoverAmount;
			const ImU32 glyphColour = glyphHover > 0.0f ? BlendColours(look.Glyph, look.HoveredGlyph, glyphHover) : look.Glyph;

			// WARNING: The glyph is drawn at its own size, not the menus': Lucide's icons fill a square one font size
			// tall, so the size given is the icon's.
			Core::PushInterfaceFont(kWindowButtonGlyphSize);

			// WARNING: Floor the position, do not round it. CalcTextSize rounds up, so a glyph centred on a half pixel is resampled and reads blurrier than the menu text beside it.
			const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
			const ImVec2 glyphPosition(std::floor(buttonMin.x + (rect.Width - glyphSize.x) * kHalf),
									   std::floor(buttonMin.y + (rect.Height - glyphSize.y) * kHalf));

			drawList->AddText(glyphPosition, glyphColour, glyph);

			ImGui::PopFont();

			return pressed;
		}

		std::optional<LogoTones> LogoTonesForTheChrome()
		{
			const ImVec4& background = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);

			if (!Core::IsLightBackground(Core::Color{.R = background.x, .G = background.y, .B = background.z, .A = background.w}))
			{
				return std::nullopt;
			}

			return LogoTones{.ForDarkest = ImGui::GetColorU32(ImGuiCol_Text), .ForLightest = ImGui::GetColorU32(ImGuiCol_TextDisabled)};
		}

		float GroupWidth(const char* glyph, const std::optional<std::string>& label)
		{
			if (!label.has_value())
			{
				return 0.0f;
			}

			return ImGui::CalcTextSize(glyph).x + Core::Scaled(kGlyphGap) + ImGui::CalcTextSize(label->c_str()).x;
		}

		void DrawGroup(const Core::CaptionRect& slot, const char* glyph, const std::string& label)
		{
			// WARNING: Place the text from the window origin and the line height, not through the cursor: BeginMenuBar() already called AlignTextToFramePadding() and that baseline offset survives SetCursorPos, so centring on top of it pushes the label low.
			const ImVec2 origin = ImGui::GetWindowPos();
			const float top = std::floor(origin.y + slot.Y + (slot.Height - ImGui::GetTextLineHeight()) * kHalf);
			const float left = std::floor(origin.x + slot.X);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddText(ImVec2(left, top), ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
			drawList->AddText(ImVec2(left + ImGui::CalcTextSize(glyph).x + Core::Scaled(kGlyphGap), top),
							  ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
		}

		void DrawDivider(const Core::CaptionRect& slot)
		{
			const ImVec2 origin = ImGui::GetWindowPos();
			const float height = std::round(slot.Height * kDividerHeightRatio);
			const float top = std::floor(origin.y + slot.Y + (slot.Height - height) * kHalf);
			const float left = std::floor(origin.x + slot.X);

			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(left + slot.Width, top + height),
													  ImGui::GetColorU32(ImGuiCol_Separator));
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

	}

	void TitleBar::RenderLogo(Core::Window& window)
	{
		if (!window.HasCustomTitleBar())
		{
			return;
		}

		DrawTitleLogo();
	}

	std::string FolderLabel(const std::filesystem::path& root)
	{
		const std::filesystem::path name = root.has_filename() ? root.filename() : root.parent_path().filename();

		return Core::ToUtf8(name.empty() ? root : name);
	}

	Core::MenuBarHeights TitleMenuHeights()
	{
		return Core::MenuBarHeights{.Row = Core::Scaled(kCaptionHeight), .Item = Core::Scaled(kMenuItemHeight)};
	}

	void DrawTitleLogo()
	{
		const float rowHeight = Core::Scaled(kCaptionHeight);
		const float height = std::round(rowHeight * kLogoHeightRatio);
		const float width = LogoWidth(height);

		const ImVec2 origin(ImGui::GetWindowPos().x + ImGui::GetCursorPosX(), ImGui::GetWindowPos().y);
		DrawLogo(ImGui::GetWindowDrawList(), ImVec2(origin.x + width * kHalf, origin.y + rowHeight * kHalf), height,
				 LogoTonesForTheChrome());

		const float halfItemSpacing = ImGui::GetStyle().ItemSpacing.x * kHalf;
		ImGui::Dummy(ImVec2(width + halfItemSpacing, 0.0f));
	}

	ImU32 WindowButtonGlyphColour()
	{
		const ImVec4* colours = ImGui::GetStyle().Colors;

		return ImGui::GetColorU32(ImLerp(colours[ImGuiCol_Text], colours[ImGuiCol_TextDisabled], kIconGreyShareOfDisabled));
	}

	WindowButton DrawWindowButtons(const Core::CaptionLayout& layout, Core::WindowState state, bool systemOwnsPointer)
	{
		const ImU32 glyph = WindowButtonGlyphColour();
		const ImU32 hoveredFill = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

		// WARNING: The held fill is ButtonActive, which the theme makes the hover fill: there is no pressed colour.
		const CaptionButtonLook plain{.HoveredFill = hoveredFill,
									  .HeldFill = ImGui::GetColorU32(ImGuiCol_ButtonActive),
									  .Glyph = glyph,
									  .HoveredGlyph = glyph};

		const CaptionButtonLook close{.HoveredFill = kCloseHoveredFill,
									  .HeldFill = kCloseHoveredFill,
									  .Glyph = glyph,
									  .HoveredGlyph = kCloseHoveredGlyph};

		WindowButton pressed = WindowButton::None;

		if (CaptionButton("CaptionMinimize", ICON_LC_MINUS, layout.Minimize, plain, systemOwnsPointer))
		{
			pressed = WindowButton::Minimize;
		}

		const char* maximizeGlyph =
			state == Core::WindowState::Maximized ? ICON_LC_COPY : ICON_LC_SQUARE;

		if (CaptionButton("CaptionMaximize", maximizeGlyph, layout.Maximize, plain, systemOwnsPointer))
		{
			pressed = WindowButton::Maximize;
		}

		if (CaptionButton("CaptionClose", ICON_LC_X, layout.Close, close, systemOwnsPointer))
		{
			pressed = WindowButton::Close;
		}

		return pressed;
	}

	Core::CaptionLayout DrawTitle(const TitleContext& context, float menuEnd, float barWidth)
	{
		const Core::CaptionTitleWidths widths{
			.Folder = GroupWidth(ICON_LC_FOLDER, context.Folder),
			.Branch = context.Folder.has_value() ? GroupWidth(ICON_LC_GIT_BRANCH, context.Branch) : 0.0f,
		};

		const Core::CaptionTitleSpacing spacing{
			.Margin = Core::Scaled(kTitleMargin),
			.DividerWidth = Core::Scaled(kDividerWidth),
			.GroupGap = Core::Scaled(kGroupGap),
			.ButtonsGap = Core::Scaled(kButtonsGap),
		};

		const Core::CaptionLayout layout = Core::LayOutCaption(
			barWidth, Core::Scaled(kCaptionHeight), Core::Scaled(kWindowButtonWidth), menuEnd, widths, spacing);

		if (layout.DividerVisible)
		{
			DrawDivider(layout.Divider);
		}

		if (layout.FolderVisible)
		{
			DrawGroup(layout.Folder, ICON_LC_FOLDER, *context.Folder);
		}

		if (layout.BranchVisible)
		{
			DrawGroup(layout.Branch, ICON_LC_GIT_BRANCH, *context.Branch);
		}

		return layout;
	}

	void TitleBar::Render(Core::Window& window, const TitleContext& context)
	{
		if (!window.HasCustomTitleBar())
		{
			return;
		}

		const float menuEnd = ImGui::GetCursorPosX();

		const float dockspaceHostWidth = ImGui::GetWindowWidth();

		const Core::CaptionLayout layout = DrawTitle(context, menuEnd, dockspaceHostWidth);

		const Core::CaptionMetrics metrics = MeasureDragRegion(menuEnd, layout.Minimize.X);

		const Core::WindowState state = window.IsMaximized() ? Core::WindowState::Maximized : Core::WindowState::Restored;

		switch (DrawWindowButtons(layout, state, PointerBelongsToSystem(window, metrics)))
		{
		case WindowButton::Minimize:
			window.Minimize();
			break;

		case WindowButton::Maximize:
			window.ToggleMaximize();
			break;

		case WindowButton::Close:
			window.RequestClose();
			break;

		case WindowButton::None:
			break;
		}

		window.SetCaptionMetrics(metrics);
	}
}
