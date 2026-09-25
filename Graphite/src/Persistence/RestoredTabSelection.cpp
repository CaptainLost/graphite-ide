#include "Persistence/RestoredTabSelection.h"

namespace Graphite {

	void RestoredTabSelection::Restore(SessionTab tab)
	{
		m_Tab = tab;
		m_Asked = false;
	}

	bool RestoredTabSelection::ShouldSelect(SessionTab tab) const
	{
		return m_Tab == tab && !m_Asked;
	}

	void RestoredTabSelection::AfterTabBar(SessionTab selected)
	{
		if (!m_Tab)
		{
			return;
		}

		// Shown, or asked for a frame ago and answered: either way the tab bar now says what
		// the reader sees.
		if (selected == *m_Tab || m_Asked)
		{
			m_Tab.reset();
			m_Asked = false;
			return;
		}

		m_Asked = true;
	}

	SessionTab RestoredTabSelection::Reported(SessionTab selected) const
	{
		return m_Tab.value_or(selected);
	}
}
