#include "Persistence/RestoredTabSelection.h"

namespace Graphite {

	void RestoredTabSelection::Restore(SessionTab tab)
	{
		m_Tab = tab;
		m_HasAsked = false;
	}

	bool RestoredTabSelection::ShouldSelect(SessionTab tab) const
	{
		return m_Tab == tab && !m_HasAsked;
	}

	void RestoredTabSelection::AfterTabBar(SessionTab selected)
	{
		if (!m_Tab)
		{
			return;
		}

		const bool tabBarShowsRestoredTab = selected == *m_Tab;
		const bool tabBarHasAnsweredTheAsk = m_HasAsked;

		if (tabBarShowsRestoredTab || tabBarHasAnsweredTheAsk)
		{
			m_Tab.reset();
			m_HasAsked = false;
			return;
		}

		m_HasAsked = true;
	}

	SessionTab RestoredTabSelection::Reported(SessionTab selected) const
	{
		return m_Tab.value_or(selected);
	}
}
