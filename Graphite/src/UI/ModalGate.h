#pragma once

#include <string_view>

namespace Graphite {

	// WARNING: ImGui cannot stack modal questions. Every OpenPopup here competes for popup level 0,
	// and opening a second one tears the first down without telling whatever was waiting on it.
	class ModalGate
	{
	public:
		bool TryOpen(std::string_view title)
		{
			if (!m_Owner.empty())
			{
				return false;
			}

			m_Owner = title;
			return true;
		}

		void Close(std::string_view title)
		{
			if (m_Owner == title)
			{
				m_Owner = std::string_view{};
			}
		}

		bool IsBusy() const { return !m_Owner.empty(); }

		bool HoldsFor(std::string_view title) const
		{
			return m_Owner == title;
		}

	private:
		std::string_view m_Owner;
	};
}