#pragma once

#include "Persistence/PersistedState.h"

#include <optional>

namespace Graphite {

	// The view a restored session window is to show, carried from the moment it is restored
	// until its tab bar has actually switched to it.
	//
	// ImGui applies a tab asked for with SetSelected on the frame after the one that asks, so
	// the frame that asks still draws the first tab as selected. Reporting that frame's
	// selection as the reader's would record the conversation for a window that was left on
	// its cost view. So the restored view stands in for the selection until the tab bar shows
	// it - or, if the reader picked another tab in that one frame, until the frame after the
	// ask, from which point the tab bar's own answer is the reader's.
	//
	// Asked only once: asking every frame would pin the reader to the view.
	class RestoredTabSelection
	{
	public:
		void Restore(SessionTab tab);

		// Whether `tab` is to be passed ImGuiTabItemFlags_SetSelected this frame.
		bool ShouldSelect(SessionTab tab) const;

		// Called once per frame the tab bar was drawn, with the tab it drew as selected.
		void AfterTabBar(SessionTab selected);

		// What to record as the window's view, given what the tab bar last drew as selected.
		SessionTab Reported(SessionTab selected) const;

		bool IsPending() const { return m_Tab.has_value(); }

	private:
		std::optional<SessionTab> m_Tab;
		bool m_Asked = false;
	};
}
