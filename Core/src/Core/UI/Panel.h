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
		enum class StartsAs
		{
			Visible,
			Hidden
		};

		// The menu group has no default: a panel that named none would land in a group no menu
		// draws and vanish from the view menu.
		Panel(std::string id, std::string title, std::string menuGroup, std::string icon,
			  StartsAs initialVisibility = StartsAs::Visible)
			: m_Id(std::move(id)), m_Title(std::move(title)), m_MenuGroup(std::move(menuGroup)),
			  m_Icon(std::move(icon)), m_Visible(initialVisibility == StartsAs::Visible),
			  m_DefaultVisible(initialVisibility == StartsAs::Visible)
		{
		}

		virtual ~Panel() = default;

		virtual void OnUpdate(Timestep) {}
		virtual void OnImGuiRender() {}

		// Called only for a panel drawn on the latest frame; one hidden, collapsed or behind
		// another tab of its dock node receives nothing.
		virtual void OnEvent(Event&) {}

		const std::string& GetId() const { return m_Id; }
		const std::string& GetTitle() const { return m_Title; }
		const std::string& GetIcon() const { return m_Icon; }

		// ImGui identifies the window by the part after ###, and imgui.ini keeps its dock
		// placement under it, so that part must not change with what the tab shows.
		std::string GetWindowName() const
		{
			const std::string tabLabel = m_Icon.empty() ? m_Title : m_Icon + kIconGap + m_Title;
			return tabLabel + kImGuiIdSeparator + m_Id;
		}

		const std::string& GetMenuGroup() const { return m_MenuGroup; }

		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		bool IsDefaultVisible() const { return m_DefaultVisible; }

		// A request rather than an immediate focus: a panel that was hidden has no window yet
		// for ImGui to focus.
		void RequestFocus() { m_FocusRequested = true; }

		bool ConsumeFocusRequest()
		{
			const bool focusWasRequested = m_FocusRequested;
			m_FocusRequested = false;
			return focusWasRequested;
		}

		bool HasBeenDrawnOnLatestFrame() const { return m_BodyRanOnLatestFrame; }
		void SetDrawnOnLatestFrame(bool bodyRan) { m_BodyRanOnLatestFrame = bodyRan; }

	private:
		static constexpr const char* kIconGap = "  ";
		static constexpr const char* kImGuiIdSeparator = "###";

		std::string m_Id;
		std::string m_Title;
		std::string m_MenuGroup;
		std::string m_Icon;
		bool m_Visible = true;
		bool m_DefaultVisible = true;
		bool m_BodyRanOnLatestFrame = false;
		bool m_FocusRequested = false;
	};
}