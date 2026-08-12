#pragma once

#include <string_view>

namespace Graphite {

	// Serialises the modal questions, because ImGui cannot stack them. Every OpenPopup
	// call here happens with an empty BeginPopupStack, so they all compete for level 0;
	// opening a second one makes ImGui ClosePopupToLevel and tear the first down. The
	// bool that asked for the destroyed popup is already consumed, so its BeginPopupModal
	// never returns true again while the state guarding the mechanism stays set forever:
	// a destroyed reload question blocks every later one, a destroyed overwrite question
	// makes Ctrl+S do nothing for the rest of the session, a destroyed close question
	// leaves the window unclosable. A question that cannot be asked now is retained by
	// its owner and asked once the gate frees, never dropped.
	class ModalGate
	{
	public:
		bool TryOpen(std::string_view title)
		{
			if (!m_Owner.empty())
				return false;

			m_Owner = title;
			return true;
		}

		void Close(std::string_view title)
		{
			if (m_Owner == title)
				m_Owner = {};
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