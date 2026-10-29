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

	// Which of the tools are drawn. A narrow row gives them up from the right: the background switch first, then Fit
	// and 1:1, then the zoom label, which leaves the two step buttons.
	enum class ImageToolsSet
	{
		All,
		WithoutBackground,
		ZoomButtons,
		StepButtons
	};

	// The width DrawImageTools takes, in screen pixels, in the path row's face. It does not depend on the zoom.
	float ImageToolsWidth(ImageToolsSet shown = ImageToolsSet::All);

	// The height of the tools' frame, in screen pixels, in the path row's face; it fits inside the path row.
	float ImageToolsHeight();

	// Zoom buttons, a zoom menu and the background swatches, drawn from the cursor along the path row. They only
	// report what was clicked; the caller applies it.
	ImageToolResult DrawImageTools(float zoom, PictureBackground background, ImageToolsSet shown = ImageToolsSet::All);
}
