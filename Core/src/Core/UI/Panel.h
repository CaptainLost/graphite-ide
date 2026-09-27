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
		// The menu group is spelled out rather than defaulted: a panel that named none would
		// register into a group no menu draws and vanish from the view menu without a word.
		// The icon is drawn before the title on the panel's tab; an empty one leaves the title
		// alone.
		Panel(std::string id, std::string title, std::string menuGroup, std::string icon,
			  bool visible = true)
			: m_Id(std::move(id)), m_Title(std::move(title)), m_MenuGroup(std::move(menuGroup)),
			  m_Icon(std::move(icon)), m_Visible(visible), m_DefaultVisible(visible)
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
		const std::string& GetIcon() const { return m_Icon; }

		// What ImGui knows the window by: the icon and title it shows on the tab, and the id
		// after ### that keeps the window, and its dock placement, the same whatever is shown.
		std::string GetWindowName() const
		{
			const std::string shown = m_Icon.empty() ? m_Title : m_Icon + "  " + m_Title;
			return shown + "###" + m_Id;
		}

		// Which block of the view menu this panel's command belongs to.
		const std::string& GetMenuGroup() const { return m_MenuGroup; }

		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		bool IsDefaultVisible() const { return m_DefaultVisible; }

		// Asks for the window to be brought forward the next time it is drawn: selected if it
		// sits behind another tab of its dock node, and focused. Kept as a request rather than
		// done on the spot because a panel that was hidden has no window yet for ImGui to focus.
		void RequestFocus() { m_FocusRequested = true; }
		bool ConsumeFocusRequest() { return std::exchange(m_FocusRequested, false); }

		// Whether this panel's body ran on the last frame, and so whether it is entitled to an
		// event. Hidden, collapsed and tab-hidden all come out the same way.
		bool WasDrawn() const { return m_WasDrawn; }
		void SetWasDrawn(bool drawn) { m_WasDrawn = drawn; }

	private:
		std::string m_Id;
		std::string m_Title;
		std::string m_MenuGroup;
		std::string m_Icon;
		bool m_Visible = true;
		bool m_DefaultVisible = true;
		bool m_WasDrawn = false;
		bool m_FocusRequested = false;
	};
}