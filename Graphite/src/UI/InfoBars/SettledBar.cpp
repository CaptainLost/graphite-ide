#include "UI/InfoBars/SettledBar.h"

#include <imgui.h>

namespace Graphite {

	bool IsPointerSettled()
	{
		const ImGuiIO& io = ImGui::GetIO();

		if (ImGui::IsAnyMouseDown())
		{
			return false;
		}

		for (const double clickedAt : io.MouseClickedTime)
		{
			if (ImGui::GetTime() - clickedAt < static_cast<double>(io.MouseDoubleClickTime))
			{
				return false;
			}
		}

		return true;
	}
}
