#include "Image/ImageTools.h"

#include "Editor/Breadcrumb.h"
#include "Image/Zoom.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace Graphite {

	namespace {
		constexpr ImU32 kDarkSwatch = IM_COL32(13, 14, 16, 255);
		constexpr ImU32 kLightSwatch = IM_COL32(243, 244, 246, 255);
		constexpr ImU32 kSwatchBorder = IM_COL32(255, 255, 255, 64);
		constexpr float kToolHeight = 22.0f;
		constexpr float kSegmentPadding = 2.0f;
		constexpr float kSegmentGap = 2.0f;
		constexpr float kSegmentBorder = 1.0f;
		constexpr float kSegmentRounding = 6.0f;
		constexpr float kSwatchButtonWidth = 22.0f;
		constexpr float kSwatchButtonRounding = 4.0f;
		constexpr float kSwatchSide = 10.0f;
		constexpr float kSwatchRounding = 2.0f;
		constexpr float kCheckerNearShare = 0.06f;
		constexpr float kCheckerFarShare = 0.14f;
		constexpr float kHalf = 0.5f;
		constexpr float kSameZoom = 0.0001f;
		constexpr std::size_t kSwatchCount = 3;
		constexpr int kToolCountBetweenGaps = 7;
		constexpr const char* kZoomMenuId = "##ImageZoomMenu";
		constexpr const char* kFitLabel = ICON_LC_MAXIMIZE " Fit";
		constexpr const char* kFitId = "##imageFit";
		constexpr bool kNotEngaged = false;

		struct ZoomChoice
		{
			const char* Label;
			float Zoom;
		};

		constexpr std::array<ZoomChoice, 7> kZoomChoices = {{{"25%", 0.25f},
															 {"50%", 0.5f},
															 {"100%", 1.0f},
															 {"200%", 2.0f},
															 {"400%", 4.0f},
															 {"800%", 8.0f},
															 {"1600%", 16.0f}}};

		float IconSide()
		{
			return Core::IconButtonSize();
		}

		std::string ZoomButtonLabel(const std::string& zoomLabel)
		{
			return zoomLabel + " " ICON_LC_CHEVRON_DOWN;
		}

		// WARNING: The tools' buttons take a frame no taller than the path row, so the frame's vertical padding is set
		// from the tool height rather than the theme's, and every size here is measured under it.
		float ToolPaddingY()
		{
			return std::max(0.0f, std::floor((Core::Scaled(kToolHeight) - ImGui::GetFontSize()) * kHalf));
		}

		class ToolFrame
		{
		public:
			ToolFrame()
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ToolPaddingY()));
			}

			~ToolFrame() { ImGui::PopStyleVar(); }

			ToolFrame(const ToolFrame&) = delete;
			ToolFrame& operator=(const ToolFrame&) = delete;
		};

		float ButtonWidthOf(const char* label)
		{
			return std::ceil(ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f);
		}

		// The widest label a zoom step can have, in the code face the label is drawn in, so the buttons beside it do
		// not move as the zoom changes.
		float ZoomButtonWidth()
		{
			Core::PushCodeFont(kBreadcrumbFontSize);
			float widest = 0.0f;

			for (const float step : kZoomSteps)
			{
				widest = std::max(widest, ButtonWidthOf(ZoomButtonLabel(ZoomLabel(step)).c_str()));
			}

			ImGui::PopFont();
			return widest;
		}

		float ActualSizeButtonWidth()
		{
			return ButtonWidthOf("1:1");
		}

		float FitButtonWidth()
		{
			return ButtonWidthOf(kFitLabel);
		}

		float SegmentInset()
		{
			return std::floor(Core::Scaled(kSegmentBorder + kSegmentPadding));
		}

		float SwatchButtonWidth()
		{
			return std::floor(Core::Scaled(kSwatchButtonWidth));
		}

		float SegmentGap()
		{
			return std::floor(Core::Scaled(kSegmentGap));
		}

		float SegmentWidth()
		{
			return SegmentInset() * 2.0f + SwatchButtonWidth() * static_cast<float>(kSwatchCount) +
				   SegmentGap() * static_cast<float>(kSwatchCount - 1);
		}

		ImageToolResult Chosen(ImageToolAction action)
		{
			return {.Action = action};
		}

		bool IsZoom(float zoom, float choice)
		{
			return std::abs(zoom - choice) < kSameZoom;
		}

		ImageToolResult DrawZoomButton(float zoom)
		{
			ImageToolResult result;

			const std::string label = ZoomButtonLabel(ZoomLabel(zoom)) + "##zoomMenu";

			Core::PushCodeFont(kBreadcrumbFontSize);
			const bool opened = Core::TextButton(label.c_str(), ImVec2(ZoomButtonWidth(), 0.0f));
			ImGui::PopFont();

			if (opened)
			{
				ImGui::OpenPopup(kZoomMenuId);
			}

			ImGui::SetItemTooltip("Zoom");

			ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));

			if (!ImGui::BeginPopup(kZoomMenuId))
			{
				return result;
			}

			if (ImGui::MenuItem("Fit"))
			{
				result = Chosen(ImageToolAction::Fit);
			}

			for (const ZoomChoice& choice : kZoomChoices)
			{
				if (ImGui::MenuItem(choice.Label, nullptr, IsZoom(zoom, choice.Zoom)))
				{
					result = {.Action = ImageToolAction::SetZoom, .Zoom = choice.Zoom};
				}
			}

			ImGui::EndPopup();
			return result;
		}

		void DrawCheckerSwatch(ImDrawList& drawList, const ImRect& rect)
		{
			const ImVec4 content = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
			const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImU32 nearColour = ImGui::GetColorU32(ImLerp(content, text, kCheckerNearShare));
			const ImU32 farColour = ImGui::GetColorU32(ImLerp(content, text, kCheckerFarShare));
			const ImVec2 middle(std::floor(rect.Min.x + rect.GetWidth() * kHalf),
								std::floor(rect.Min.y + rect.GetHeight() * kHalf));

			drawList.AddRectFilled(rect.Min, middle, nearColour);
			drawList.AddRectFilled(middle, rect.Max, nearColour);
			drawList.AddRectFilled(ImVec2(middle.x, rect.Min.y), ImVec2(rect.Max.x, middle.y), farColour);
			drawList.AddRectFilled(ImVec2(rect.Min.x, middle.y), ImVec2(middle.x, rect.Max.y), farColour);
		}

		void DrawSwatch(ImDrawList& drawList, PictureBackground kind, const ImRect& cell)
		{
			const float side = std::floor(Core::Scaled(kSwatchSide));
			const ImVec2 corner(std::floor((cell.Min.x + cell.Max.x - side) * kHalf),
								std::floor((cell.Min.y + cell.Max.y - side) * kHalf));
			const ImRect swatch(corner, ImVec2(corner.x + side, corner.y + side));
			const float rounding = Core::Scaled(kSwatchRounding);

			switch (kind)
			{
			case PictureBackground::Checkerboard:
				DrawCheckerSwatch(drawList, swatch);
				break;
			case PictureBackground::Dark:
				drawList.AddRectFilled(swatch.Min, swatch.Max, kDarkSwatch, rounding);
				break;
			case PictureBackground::Light:
				drawList.AddRectFilled(swatch.Min, swatch.Max, kLightSwatch, rounding);
				break;
			}

			drawList.AddRect(swatch.Min, swatch.Max, kSwatchBorder, rounding);
		}

		bool DrawSwatchButton(const char* id, PictureBackground kind, bool current, const char* tooltip, ImVec2 size)
		{
			const bool pressed = ImGui::InvisibleButton(id, size);
			const ImRect cell(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			ImDrawList& drawList = *ImGui::GetWindowDrawList();

			const ImVec4 fill =
				Core::IconButtonFill(ImGui::GetItemID(), ImGui::IsItemHovered(), ImGui::IsItemActive(), current);
			drawList.AddRectFilled(cell.Min, cell.Max, ImGui::GetColorU32(fill), Core::Scaled(kSwatchButtonRounding));

			DrawSwatch(drawList, kind, cell);
			ImGui::SetItemTooltip("%s", tooltip);

			return pressed;
		}

		// A bordered segment holding the three swatch buttons. Its buttons are placed by position, and the segment is
		// then laid out as one item of the frame's height.
		ImageToolResult DrawBackgroundSegment(PictureBackground background)
		{
			struct Swatch
			{
				const char* Id;
				PictureBackground Kind;
				const char* Tooltip;
			};

			constexpr std::array<Swatch, kSwatchCount> swatches = {{{"##imageCheckerboard",
																	  PictureBackground::Checkerboard,
																	  "Checkerboard background"},
																	 {"##imageDark", PictureBackground::Dark, "Dark background"},
																	 {"##imageLight", PictureBackground::Light, "Light background"}}};

			ImageToolResult result;

			const ImVec2 start = ImGui::GetCursorScreenPos();
			const ImVec2 size(SegmentWidth(), ImGui::GetFrameHeight());
			const float inset = SegmentInset();
			const ImVec2 button(SwatchButtonWidth(), std::max(1.0f, size.y - inset * 2.0f));

			ImGui::GetWindowDrawList()->AddRect(start, ImVec2(start.x + size.x, start.y + size.y),
												ImGui::GetColorU32(ImGuiCol_Border), Core::Scaled(kSegmentRounding));

			float x = start.x + inset;

			for (const Swatch& swatch : swatches)
			{
				ImGui::SetCursorScreenPos(ImVec2(x, start.y + inset));

				if (DrawSwatchButton(swatch.Id, swatch.Kind, swatch.Kind == background, swatch.Tooltip, button))
				{
					result = {.Action = ImageToolAction::SetBackground, .Background = swatch.Kind};
				}

				x += button.x + SegmentGap();
			}

			ImGui::SetCursorScreenPos(start);
			ImGui::Dummy(size);

			return result;
		}

		ImageToolResult DrawTools(float zoom, PictureBackground background)
		{
			ImageToolResult result;

			if (Core::DrawIconButton("##imageZoomOut", ICON_LC_MINUS, kNotEngaged, "Zoom out"))
			{
				result = Chosen(ImageToolAction::ZoomOut);
			}

			ImGui::SameLine();

			const ImageToolResult fromMenu = DrawZoomButton(zoom);

			if (fromMenu.Action != ImageToolAction::None)
			{
				result = fromMenu;
			}

			ImGui::SameLine();

			if (Core::DrawIconButton("##imageZoomIn", ICON_LC_PLUS, kNotEngaged, "Zoom in"))
			{
				result = Chosen(ImageToolAction::ZoomIn);
			}

			ImGui::SameLine();
			Core::DrawIconSeparator();
			ImGui::SameLine();

			if (Core::TextButton((std::string(kFitLabel) + kFitId).c_str(), ImVec2(FitButtonWidth(), 0.0f)))
			{
				result = Chosen(ImageToolAction::Fit);
			}

			ImGui::SameLine();

			if (Core::TextButton("1:1", ImVec2(ActualSizeButtonWidth(), 0.0f)))
			{
				result = Chosen(ImageToolAction::ActualSize);
			}

			ImGui::SetItemTooltip("Actual size");

			ImGui::SameLine();

			if (const ImageToolResult fromSegment = DrawBackgroundSegment(background);
				fromSegment.Action != ImageToolAction::None)
			{
				result = fromSegment;
			}

			return result;
		}
	}

	float ImageToolsWidth()
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const ToolFrame frame;

		const float gaps = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(kToolCountBetweenGaps - 1);
		const float width = IconSide() * 2.0f + ZoomButtonWidth() + FitButtonWidth() + ActualSizeButtonWidth() +
							SegmentWidth() + Core::Scaled(Core::kIconSeparatorWidth) + gaps;

		ImGui::PopFont();
		return width;
	}

	float ImageToolsHeight()
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const ToolFrame frame;
		const float height = ImGui::GetFrameHeight();

		ImGui::PopFont();
		return height;
	}

	ImageToolResult DrawImageTools(float zoom, PictureBackground background)
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const ToolFrame frame;
		const ImageToolResult result = DrawTools(zoom, background);
		ImGui::PopFont();

		return result;
	}
}
