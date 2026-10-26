#pragma once

#include "Image/PictureBackground.h"

namespace Graphite {

	enum class ImageToolAction
	{
		None,
		ZoomIn,
		ZoomOut,
		Fit,
		ActualSize,
		SetZoom,
		SetBackground
	};

	struct ImageToolResult
	{
		ImageToolAction Action = ImageToolAction::None;
		float Zoom = 0.0f;
		PictureBackground Background = PictureBackground::Checkerboard;
	};

	// The width DrawImageTools takes, in screen pixels, in the path row's face. It does not depend on the zoom.
	float ImageToolsWidth();

	// Zoom buttons, a zoom menu and the background swatches, drawn from the cursor along the path row. They only
	// report what was clicked; the caller applies it.
	ImageToolResult DrawImageTools(float zoom, PictureBackground background);
}
