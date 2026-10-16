#include "UI/LoaderDelay.h"

#include "Core/App/Redraw.h"

#include <imgui.h>

#include <chrono>

namespace Graphite {

	bool LoaderIsDue(std::optional<double>& waitingSince)
	{
		const double now = ImGui::GetTime();

		if (!waitingSince.has_value())
		{
			waitingSince = now;
		}

		const double secondsWaited = now - *waitingSince;

		if (secondsWaited < kSecondsBeforeLoaderShows)
		{
			Core::Redraw::RequestIn(std::chrono::duration<double>(kSecondsBeforeLoaderShows - secondsWaited));
			return false;
		}

		return true;
	}
}
