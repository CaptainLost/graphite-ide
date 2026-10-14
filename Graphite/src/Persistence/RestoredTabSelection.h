#pragma once

#include "Persistence/PersistedState.h"

#include <optional>

namespace Graphite {

	// WARNING: ImGui applies a tab asked for with SetSelected on the frame after the one that
	// asks, so that frame still draws the first tab as selected. The restored tab therefore
	// stands in for the selection until the tab bar shows it, and it is asked for only once.
	class RestoredTabSelection
	{
	public:
		void Restore(SessionTab tab);

		bool ShouldSelect(SessionTab tab) const;

		void AfterTabBar(SessionTab selected);

		SessionTab Reported(SessionTab selected) const;

		bool IsPending() const { return m_Tab.has_value(); }

	private:
		std::optional<SessionTab> m_Tab;
		bool m_HasAsked = false;
	};
}
