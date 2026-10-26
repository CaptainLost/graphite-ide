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
		constexpr float kSwatchInset = 4.0f;
		constexpr float kCheckerNearShare = 0.06f;
		constexpr float kCheckerFarShare = 0.14f;
		constexpr float kHalf = 0.5f;
		constexpr float kSameZoom = 0.0001f;
		constexpr int kButtonCountBetweenGaps = 9;
		constexpr const char* kZoomMenuId = "##ImageZoomMenu";
		constexpr const char* kNoGlyph = "";
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

		// The widest label a zoom step can have, so the buttons beside it do not move as the zoom changes.
		float ZoomButtonWidth()
		{
			float widest = 0.0f;

			for (const float step : kZoomSteps)
			{
				widest = std::max(widest, ImGui::CalcTextSize(ZoomButtonLabel(ZoomLabel(step)).c_str()).x);
			}

			return std::ceil(widest + ImGui::GetStyle().FramePadding.x * 2.0f);
		}

		float ActualSizeButtonWidth()
		{
			return std::ceil(ImGui::CalcTextSize("1:1").x + ImGui::GetStyle().FramePadding.x * 2.0f);
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

			if (Core::TextButton(label.c_str(), ImVec2(ZoomButtonWidth(), 0.0f)))
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

		bool DrawSwatch(const char* id, PictureBackground kind, bool current, const char* tooltip)
		{
			const bool pressed = Core::DrawIconButton(id, kNoGlyph, current, tooltip);

			const ImRect cell(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			ImDrawList& drawList = *ImGui::GetWindowDrawList();

			if (current)
			{
				drawList.AddRectFilled(cell.Min, cell.Max, ImGui::GetColorU32(ImGuiCol_ButtonActive),
									   ImGui::GetStyle().FrameRounding);
			}

			const float inset = std::floor(Core::Scaled(kSwatchInset));
			const ImRect swatch(ImVec2(cell.Min.x + inset, cell.Min.y + inset),
								ImVec2(cell.Max.x - inset, cell.Max.y - inset));

			switch (kind)
			{
			case PictureBackground::Checkerboard:
				DrawCheckerSwatch(drawList, swatch);
				break;
			case PictureBackground::Dark:
				drawList.AddRectFilled(swatch.Min, swatch.Max, kDarkSwatch);
				break;
			case PictureBackground::Light:
				drawList.AddRectFilled(swatch.Min, swatch.Max, kLightSwatch);
				break;
			}

			return pressed;
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

			if (Core::DrawIconButton("##imageFit", ICON_LC_MAXIMIZE, kNotEngaged, "Fit"))
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

			struct Swatch
			{
				const char* Id;
				PictureBackground Kind;
				const char* Tooltip;
			};

			constexpr std::array<Swatch, 3> swatches = {{{"##imageCheckerboard", PictureBackground::Checkerboard,
														  "Checkerboard background"},
														 {"##imageDark", PictureBackground::Dark, "Dark background"},
														 {"##imageLight", PictureBackground::Light, "Light background"}}};

			for (std::size_t index = 0; index < swatches.size(); ++index)
			{
				if (index > 0)
				{
					ImGui::SameLine();
				}

				const Swatch& swatch = swatches[index];

				if (DrawSwatch(swatch.Id, swatch.Kind, swatch.Kind == background, swatch.Tooltip))
				{
					result = {.Action = ImageToolAction::SetBackground, .Background = swatch.Kind};
				}
			}

			return result;
		}
	}

	float ImageToolsWidth()
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);

		const float side = IconSide();
		const float iconButtons = side * 3.0f;
		const float swatches = side * 3.0f;
		const float gaps = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(kButtonCountBetweenGaps - 1);
		const float width =
			iconButtons + swatches + ZoomButtonWidth() + ActualSizeButtonWidth() + Core::Scaled(Core::kIconSeparatorWidth) + gaps;

		ImGui::PopFont();
		return width;
	}

	ImageToolResult DrawImageTools(float zoom, PictureBackground background)
	{
		Core::PushInterfaceFont(kBreadcrumbFontSize);
		const ImageToolResult result = DrawTools(zoom, background);
		ImGui::PopFont();

		return result;
	}
}
