#include "Image/ImageTabView.h"

#include "Editor/EditorSettings.h"
#include "Image/ImageStatus.h"
#include "Image/ImageTab.h"
#include "Image/ImageTools.h"
#include "Image/PictureCanvas.h"

#include "Core/App/Redraw.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/TextButton.h"

#include <cmath>
#include <optional>
#include <string>

namespace Graphite {

	namespace {
		constexpr const char* kLoadingText = "Loading image…";
		constexpr const char* kRetryLabel = "Retry";
		constexpr const char* kReloadRetryLabel = "Retry##reloadImage";
		constexpr const char* kCanvasId = "##picture";
		constexpr float kHalf = 0.5f;
		constexpr float kActualSize = 1.0f;

		struct DrawnPicture
		{
			ImRect Area;
			std::optional<HoveredPixel> Hovered;
		};

		ImVec2 PictureSizeOf(const ImageFacts& facts)
		{
			return ImVec2(static_cast<float>(facts.Width), static_cast<float>(facts.Height));
		}

		bool HasRoom(const ImRect& area)
		{
			return area.GetWidth() > 0.0f && area.GetHeight() > 0.0f;
		}

		void ZoomTo(ImageTab& tab, float zoom, const ImRect& area)
		{
			const ImageFacts* facts = tab.GetFacts();

			if (facts == nullptr || !HasRoom(area))
			{
				return;
			}

			tab.GetZoom() = ZoomAround(tab.GetZoom(), zoom, area.GetCenter(), PictureSizeOf(*facts), area, PanKeepVisible());
		}

		// WARNING: The row puts the tools at its top edge. Their frame is centred in the row's height here, measured in
		// the row's face as the tools draw in it; rounding down keeps their words on the line the path's words stand on.
		void CentreInRow()
		{
			const float frameHeight = ImageToolsHeight();

			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + std::floor((BreadcrumbHeight() - frameHeight) * kHalf)));
		}

		// The tools are drawn above the picture but act on the area the canvas lays out below them, so what they ask
		// for is applied once the canvas is drawn and shows from the next frame.
		void ApplyTool(const ImageToolResult& tool, ImageTab& tab, EditorSettings& settings, const ImRect& area)
		{
			switch (tool.Action)
			{
			case ImageToolAction::None:
				return;
			case ImageToolAction::ZoomIn:
				ZoomImageTab(tab, PictureZoom::In, area);
				break;
			case ImageToolAction::ZoomOut:
				ZoomImageTab(tab, PictureZoom::Out, area);
				break;
			case ImageToolAction::Fit:
				ZoomImageTab(tab, PictureZoom::Fit, area);
				break;
			case ImageToolAction::ActualSize:
				ZoomTo(tab, kActualSize, area);
				break;
			case ImageToolAction::SetZoom:
				ZoomTo(tab, tool.Zoom, area);
				break;
			case ImageToolAction::SetBackground:
				settings.SetImageBackground(tool.Background);
				break;
			}

			Core::Redraw::Request();
		}

		// One line above the picture: why it is not the file's latest version, and Retry.
		bool DrawReloadNotice(const std::string& notice)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(notice.c_str());
			ImGui::PopStyleColor();

			ImGui::SameLine();
			return Core::SmallTextButton(kReloadRetryLabel);
		}

		DrawnPicture DrawPicture(ImageTab& tab, EditorSettings& settings, const ImageToolResult& tool)
		{
			if (!tab.GetReloadFailure().empty() && DrawReloadNotice(tab.GetReloadFailure()))
			{
				tab.Load();
			}

			PictureCanvasInput input;
			input.Texture = tab.GetTexture();
			input.Picture = PictureSizeOf(*tab.GetFacts());
			input.Background = settings.GetImageBackground();

			const PictureCanvasResult canvas = DrawPictureCanvas(kCanvasId, tab.GetZoom(), input);
			ApplyTool(tool, tab, settings, canvas.Area);

			DrawnPicture drawn{.Area = canvas.Area};

			if (canvas.Hovered)
			{
				drawn.Hovered = tab.PixelAt(*canvas.Hovered);
			}

			return drawn;
		}
	}

	ImageTabFrame DrawImageTab(ImageTab& tab, const BreadcrumbModel& path, EditorSettings& settings)
	{
		ImageTabFrame frame;
		frame.Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

		// Only a picture on screen has anything for the tools to act on.
		const bool showsPicture = tab.GetState() == ImageTabState::Ready && tab.GetFacts() != nullptr;
		ImageToolResult tool;
		BreadcrumbTools tools;

		if (showsPicture)
		{
			const auto toolsOf = [&tool, &tab, &settings](ImageToolsSet shown) {
				BreadcrumbTools set;
				set.Width = ImageToolsWidth(shown);
				set.Draw = [&tool, &tab, &settings, shown] {
					CentreInRow();
					tool = DrawImageTools(tab.GetZoom().Zoom, settings.GetImageBackground(), shown);
				};
				return set;
			};

			tools = toolsOf(ImageToolsSet::All);
			tools.Narrower = {toolsOf(ImageToolsSet::WithoutBackground), toolsOf(ImageToolsSet::ZoomButtons),
							  toolsOf(ImageToolsSet::StepButtons)};
		}

		DrawBreadcrumb(path, tools);

		std::optional<HoveredPixel> hovered;

		switch (tab.GetState())
		{
		case ImageTabState::Loading:
			Core::LoadingState(kLoadingText);
			break;
		case ImageTabState::Failed:
			if (Core::EmptyState(tab.GetFailure(), tab.CanRetry() ? kRetryLabel : nullptr))
			{
				tab.Load();
			}
			break;
		case ImageTabState::Ready:
		{
			const DrawnPicture drawn = DrawPicture(tab, settings, tool);
			frame.Area = drawn.Area;
			hovered = drawn.Hovered;
			break;
		}
		}

		if (const ImageFacts* facts = tab.GetFacts())
		{
			frame.Status = ImageStatusItems(*facts, hovered);
		}

		return frame;
	}

	void ZoomImageTab(ImageTab& tab, PictureZoom zoom, const ImRect& area)
	{
		const ImageFacts* facts = tab.GetFacts();

		if (facts == nullptr || !HasRoom(area))
		{
			return;
		}

		ZoomState& state = tab.GetZoom();

		switch (zoom)
		{
		case PictureZoom::In:
			ZoomTo(tab, StepIn(state.Zoom), area);
			return;
		case PictureZoom::Out:
			ZoomTo(tab, StepOut(state.Zoom), area);
			return;
		case PictureZoom::Fit:
			state.Zoom = FitZoom(PictureSizeOf(*facts), area.GetSize(), FitMargin());
			state.Pan = ImVec2(0.0f, 0.0f);
			return;
		}
	}
}
