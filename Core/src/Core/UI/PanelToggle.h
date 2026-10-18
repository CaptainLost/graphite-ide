#pragma once

namespace Core {

	enum class PanelToggle
	{
		ShowAndBringForward,
		BringForward,
		Hide
	};

	PanelToggle ChoosePanelToggle(bool visible, bool drawnOnLatestFrame);
}
