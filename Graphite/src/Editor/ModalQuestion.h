#pragma once

#include "Editor/ModalGate.h"

#include <imgui.h>

#include <algorithm>
#include <concepts>
#include <optional>
#include <utility>
#include <vector>

namespace Graphite {

	template<std::movable Payload>
	class ModalQuestion
	{
	public:
		ModalQuestion(ModalGate& gate, const char* title)
			: m_Gate(gate), m_Title(title)
		{
		}

		void Ask(Payload payload) { m_Queue.push_back(std::move(payload)); }

		bool IsAsked() const
		{
			return m_Current.has_value() || !m_Queue.empty();
		}

		bool Asked(const Payload& payload) const
		{
			if (m_Current && *m_Current == payload)
			{
				return true;
			}

			return std::find(m_Queue.begin(), m_Queue.end(), payload) != m_Queue.end();
		}

		const Payload* GetCurrent() const
		{
			return m_Current ? &*m_Current : nullptr;
		}

		template<std::predicate<const Payload&> Predicate>
		bool Begin(Predicate stillWorthAsking)
		{
			OpenNext(stillWorthAsking);

			if (ImGui::BeginPopupModal(m_Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				return true;
			}

			// Believed on screen by the gate and yet not open: dismissed with Escape, or
			// destroyed by something opening over it. Treated as no answer at all - what
			// the question was about is untouched, and the site is free to ask again.
			if (m_Gate.HoldsFor(m_Title) && !ImGui::IsPopupOpen(m_Title))
			{
				m_Current.reset();
				m_Gate.Close(m_Title);
			}

			return false;
		}

		bool Begin()
		{
			return Begin([](const Payload&) { return true; });
		}

		void Answer()
		{
			m_Current.reset();
			m_Gate.Close(m_Title);
			ImGui::CloseCurrentPopup();
		}

		void End() { ImGui::EndPopup(); }

	private:
		template<std::predicate<const Payload&> Predicate>
		void OpenNext(Predicate& stillWorthAsking)
		{
			while (!m_Current && !m_Queue.empty() && !m_Gate.IsBusy())
			{
				Payload candidate = std::move(m_Queue.front());
				m_Queue.erase(m_Queue.begin());

				if (!stillWorthAsking(std::as_const(candidate)))
				{
					continue;
				}

				// Asked again even though the loop condition just found the gate free,
				// because stillWorthAsking runs in between and runs application code: a
				// predicate that acts on the question having gone stale - closing the tab
				// it was about, carrying out the close it was asking permission for - can
				// reach something that opens a question of its own and takes the level.
				if (!m_Gate.TryOpen(m_Title))
				{
					// It did. Put the question back where it was rather than dropping it,
					// and wait for whichever question now holds the level.
					m_Queue.insert(m_Queue.begin(), std::move(candidate));
					return;
				}

				m_Current = std::move(candidate);
				ImGui::OpenPopup(m_Title);
			}
		}

		ModalGate& m_Gate;

		const char* m_Title;

		std::vector<Payload> m_Queue;

		std::optional<Payload> m_Current;
	};
}