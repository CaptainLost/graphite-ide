#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/Event.h"

#include <string>
#include <utility>

namespace Core {

	class Panel : NonCopyable
	{
	public:
		Panel(std::string id, std::string title, bool visible = true)
			: m_Id(std::move(id)), m_Title(std::move(title)), m_Visible(visible),
			  m_DefaultVisible(visible)
		{
		}

		virtual ~Panel() = default;

		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnImGuiRender() {}

		// Delivered only to panels whose body ran on the previous frame, so a panel behind
		// another tab of the same dock node receives nothing.
		virtual void OnEvent(Event& event) { (void)event; }

		const std::string& GetId() const { return m_Id; }
		const std::string& GetTitle() const { return m_Title; }

		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		bool IsDefaultVisible() const { return m_DefaultVisible; }

		// Whether this panel's body ran on the last frame, and so whether it is entitled to an
		// event. Hidden, collapsed and tab-hidden all come out the same way.
		bool WasDrawn() const { return m_WasDrawn; }
		void SetWasDrawn(bool drawn) { m_WasDrawn = drawn; }

	private:
		std::string m_Id;
		std::string m_Title;
		bool m_Visible = true;
		bool m_DefaultVisible = true;
		bool m_WasDrawn = false;
	};
}