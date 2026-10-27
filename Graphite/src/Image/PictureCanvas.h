#pragma once

#include "Image/PictureBackground.h"
#include "Image/Zoom.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

namespace Core {
	class ImageTexture;
}

namespace Graphite {

	struct PictureCanvasInput
	{
		const Core::ImageTexture* Texture = nullptr;
		// The file's dimensions, which the zoom works in. A texture scaled down to the renderer's limit is stretched
		// to them.
		ImVec2 Picture;
		PictureBackground Background = PictureBackground::Checkerboard;
		bool PixelGrid = true;
	};

	struct PictureCanvasResult
	{
		std::optional<PixelPoint> Hovered;
		ImRect Area;
	};

	// Lays out the rest of the content region as one item and draws the picture in it, zoomed with the wheel and
	// panned by dragging.
	PictureCanvasResult DrawPictureCanvas(const char* id, ZoomState& zoom, const PictureCanvasInput& input);
}
