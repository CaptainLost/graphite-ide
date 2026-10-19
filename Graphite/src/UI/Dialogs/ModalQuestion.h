#pragma once

#include "Core/Base/Assert.h"
#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/DialogLayout.h"

#include <imgui.h>

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	// WARNING: ImGui identifies a window by its name from the first "###" on, so nothing in the
	// visible part may start one: a branch called "a###b", or "fix#" running into the stable part as
	// "fix####", would lose the popup and leave the modal gate held by a question nobody can see.
	inline std::string ModalWindowName(std::string visible, std::string_view stableId)
	{
		constexpr std::string_view kDoubleHash = "##";

		for (std::size_t hashStart = visible.find(kDoubleHash); hashStart != std::string::npos;
			 hashStart = visible.find(kDoubleHash, hashStart + kDoubleHash.size()))
		{
			visible.insert(hashStart + 1, " ");
		}

		if (!visible.empty() && visible.back() == '#')
		{
			visible += ' ';
		}

		visible += stableId;
		return visible;
	}

	template<std::movable Payload>
	class ModalQuestion
	{
	public:
		ModalQuestion(ModalGate& gate, const char* title)
			: m_Gate(gate), m_Title(title)
		{
		}

		void Ask(Payload payload) { m_Queue.push_back(std::move(payload)); }

		void AskOnce(Payload payload)
		{
			if (!Asked(payload))
			{
				Ask(std::move(payload));
			}
		}

		bool IsAsked() const
		{
			return m_Current.has_value() || !m_Queue.empty();
		}

		bool Asked(const Payload& payload) const
		{
			if (m_Current && IsSameQuestion(*m_Current, payload))
			{
				return true;
			}

			for (const Payload& queued : m_Queue)
			{
				if (IsSameQuestion(queued, payload))
				{
					return true;
				}
			}

			return false;
		}

		// WARNING: Null when the popup is not open this frame, and End must then not be called.
		template<std::predicate<const Payload&> Predicate>
		const Payload* Begin(Predicate stillWorthAsking)
		{
			OpenNext(stillWorthAsking);
			return BeginNamed(m_Title);
		}

		const Payload* Begin()
		{
			return Begin(&AlwaysWorthAsking);
		}

		template<std::predicate<const Payload&> Predicate, std::invocable<const Payload&> TitleOf>
		const Payload* Begin(Predicate stillWorthAsking, TitleOf titleOf)
		{
			CORE_ASSERT(std::string_view(m_Title).starts_with("###"),
						"a question with a visible title needs a title of its own that starts with ###");

			OpenNext(stillWorthAsking);

			const std::string name =
				ModalWindowName(m_Current ? std::string(titleOf(std::as_const(*m_Current))) : std::string(), m_Title);

			return BeginNamed(name.c_str());
		}

		void Answer()
		{
			m_Current.reset();
			m_Gate.Close(m_Title);
			ImGui::CloseCurrentPopup();
		}

		void End()
		{
			ImGui::EndPopup();
		}

		// WARNING: no CloseCurrentPopup here: it is only valid between a popup's Begin and End, and
		// this is called from outside one.
		void Withdraw()
		{
			m_Current.reset();
			m_Queue.clear();
			m_Gate.Close(m_Title);
		}

		// WARNING: the popup of a withdrawn current question stays on screen until the owner's next Begin
		// closes it.
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
		static bool AlwaysWorthAsking(const Payload&)
		{
			return true;
		}

		static bool IsSameQuestion(const Payload& askedPayload, const Payload& otherPayload)
		{
			if constexpr (requires { { askedPayload.IsSameQuestion(otherPayload) } -> std::convertible_to<bool>; })
			{
				return askedPayload.IsSameQuestion(otherPayload);
			}
			else
			{
				return askedPayload == otherPayload;
			}
		}

		const Payload* BeginNamed(const char* name)
		{
			if (!ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				WithdrawIfNoLongerOnScreen();
				return nullptr;
			}

			// WARNING: Withdraw cannot close the popup from outside it, so a question withdrawn while on
			// screen leaves its popup open with nothing to ask, and it is closed here.
			if (!m_Current)
			{
				ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
				return nullptr;
			}

			return &*m_Current;
		}

		// WARNING: ImGui closes a popup on Escape, and destroys it when another opens over it, without
		// either reaching the code that asked the question.
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

				// WARNING: the gate is tried again although the loop condition found it free, because
				// stillWorthAsking runs application code that can itself ask a question and take it.
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