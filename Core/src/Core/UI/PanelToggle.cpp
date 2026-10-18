#include "corepch.h"

#include "Core/UI/PanelToggle.h"

namespace Core {

	PanelToggle ChoosePanelToggle(bool visible, bool drawnOnLatestFrame)
	{
		if (!visible)
		{
			return PanelToggle::ShowAndBringForward;
		}

		return drawnOnLatestFrame ? PanelToggle::Hide : PanelToggle::BringForward;
	}
}
