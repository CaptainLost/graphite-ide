#pragma once

#include "Editor/ModalGate.h"

#include <imgui.h>

#include <algorithm>
#include <concepts>
#include <optional>
#include <string>
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
			return BeginNamed(m_Title);
		}

		bool Begin()
		{
			return Begin([](const Payload&) { return true; });
		}

		// For a title that names what is being asked about - "Delete branch feature/x". The
		// question's own title has to start with "###": ImGui identifies a window by its name
		// from "###" on and shows only what comes before it, so the popup, and the gate held
		// under the stable title, stay the same while the visible part follows the payload.
		template<std::predicate<const Payload&> Predicate, std::invocable<const Payload&> TitleOf>
		bool Begin(Predicate stillWorthAsking, TitleOf titleOf)
		{
			OpenNext(stillWorthAsking);

			std::string name = m_Current ? std::string(titleOf(std::as_const(*m_Current))) : std::string();

			// A branch may be called "a###b"; ImGui would read that as the window's identity
			// and the popup opened under m_Title would never be found again.
			for (std::size_t at = name.find("##"); at != std::string::npos; at = name.find("##", at + 2))
			{
				name.insert(at + 1, " ");
			}

			name += m_Title;
			return BeginNamed(name.c_str());
		}

		void Answer()
		{
			m_Current.reset();
			m_Gate.Close(m_Title);
			ImGui::CloseCurrentPopup();
		}

		void End() { ImGui::EndPopup(); }

		// Abandons the question and hands the gate back. Begin is the only other code that
		// releases the gate, and it runs from the owner's render — an owner that stops being
		// rendered while a question is open would hold the gate forever, and no modal question
		// anywhere in the application could be asked again. No CloseCurrentPopup here: that is
		// only valid between a popup's Begin and End, and this is called from outside one.
		void Withdraw()
		{
			m_Current.reset();
			m_Queue.clear();
			m_Gate.Close(m_Title);
		}

		// Abandons only the questions `matches` selects, for when something answered them
		// without the user being asked. A selected current question hands the gate back like
		// Withdraw does; the popup it opened is still on screen until the owner's render sees
		// GetCurrent() come back null and closes it.
		template<std::predicate<const Payload&> Predicate>
		void WithdrawIf(Predicate matches)
		{
			std::erase_if(m_Queue, matches);

			if (m_Current && matches(std::as_const(*m_Current)))
			{
				m_Current.reset();
				m_Gate.Close(m_Title);
			}
		}

	private:
		bool BeginNamed(const char* name)
		{
			if (ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				return true;
			}

			WithdrawIfNoLongerOnScreen();

			return false;
		}

		// ImGui closes a popup on Escape, and destroys it when another opens over it,
		// without either reaching the code that asked the question.
		void WithdrawIfNoLongerOnScreen()
		{
			if (!m_Gate.HoldsFor(m_Title) || ImGui::IsPopupOpen(m_Title))
			{
				return;
			}

			m_Current.reset();
			m_Gate.Close(m_Title);
		}

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

				// Asked again even though the loop condition just found the gate free:
				// stillWorthAsking runs application code in between, which can itself ask a
				// question and take the gate.
				if (!m_Gate.TryOpen(m_Title))
				{
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