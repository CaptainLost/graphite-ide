#pragma once

#include "Editor/Breadcrumb.h"
#include "Editor/StatusBar/StatusBar.h"
#include "Image/Zoom.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <vector>

namespace Graphite {

	class EditorSettings;
	class ImageTab;

	struct ImageTabFrame
	{
		std::vector<ViewStatusItem> Status;

		// Whether the tab's window holds the keyboard focus, a child of it included.
		bool Focused = false;

		// Where the picture is laid out, which the zoom shortcuts zoom around; empty while there is no picture.
		ImRect Area;
	};

	// One image tab's content in the window being drawn: the path row with the image tools on its right, then the
	// picture, the loader while it loads, or the reason it could not be read, with Retry where trying again can help.
	ImageTabFrame DrawImageTab(ImageTab& tab, const BreadcrumbModel& path, EditorSettings& settings);

	// Steps or fits the picture around the centre of `area`, as its tools do. A tab with no picture is left alone.
	void ZoomImageTab(ImageTab& tab, PictureZoom zoom, const ImRect& area);
}
