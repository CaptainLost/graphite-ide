#include "Editor/Navigation/NavigationHistory.h"

#include <utility>

namespace Graphite {

	void NavigationHistory::NoteDeparture(NavigationPoint from)
	{
		m_Forward.clear();

		if (!m_Back.empty() && m_Back.back() == from)
		{
			return;
		}

		m_Back.push_back(std::move(from));

		if (m_Back.size() > kLongestHistory)
		{
			m_Back.erase(m_Back.begin());
		}
	}

	std::optional<NavigationPoint> NavigationHistory::Back(NavigationPoint current)
	{
		if (m_Back.empty())
		{
			return std::nullopt;
		}

		NavigationPoint target = std::move(m_Back.back());
		m_Back.pop_back();
		m_Forward.push_back(std::move(current));

		return target;
	}

	std::optional<NavigationPoint> NavigationHistory::Forward(NavigationPoint current)
	{
		if (m_Forward.empty())
		{
			return std::nullopt;
		}

		NavigationPoint target = std::move(m_Forward.back());
		m_Forward.pop_back();
		m_Back.push_back(std::move(current));

		return target;
	}
}
