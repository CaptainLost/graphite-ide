#include "UI/TabWindow.h"

#include "UI/RememberedPlace.h"

#include "Core/ImGui/TabCloseRequests.h"

#include <imgui_internal.h>

#include <utility>

namespace Graphite {

	namespace {
		constexpr ImGuiID kNoDockNode = 0;
	}

	TabWindowFrame BeginTabWindow(const std::string& name, TabWindowState& state, const PlaceNewTab& place,
								  Core::TabCloseRequests& closeRequests, ImGuiWindowFlags flags)
	{
		TabWindowFrame frame;
		frame.Appearing = std::exchange(state.Fresh, false);

		const bool restored = frame.Appearing && std::exchange(state.Restored, false);

		if (frame.Appearing && (!restored || !HasRememberedPlace(name)))
		{
			place(name);
		}

		if (std::exchange(state.Focus, false))
		{
			ImGui::SetNextWindowFocus();
		}

		// WARNING: ImGui focuses every window on the frame it appears, and focusing a docked window
		// selects its tab, so without this the last restored window drawn would take the front of its
		// node from whatever imgui.ini remembers there.
		if (restored)
		{
			flags |= ImGuiWindowFlags_NoFocusOnAppearing;
		}

		frame.Visible = ImGui::Begin(name.c_str(), &frame.Open, flags);

		if (closeRequests.Take(name))
		{
			frame.Open = false;
		}

		return frame;
	}

	PlaceNewTab DockInto(ImGuiID node)
	{
		return [node](const std::string& windowName) {
			if (node != kNoDockNode)
			{
				ImGui::DockBuilderDockWindow(windowName.c_str(), node);
			}
		};
	}
}
