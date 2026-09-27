#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/UI/Panel.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;
	class TabCloseRequests;

	class PanelManager final : NonCopyable
	{
	public:
		Panel* Add(Scope<Panel> panel);

		Panel* Find(std::string_view id);
		std::size_t GetCount() const { return m_Panels.size(); }

		bool SetVisible(std::string_view id, bool visible);

		void ForEach(const std::function<void(Panel&)>& visit);

		void OnUpdate(Timestep ts);
		void OnImGuiRender(TabCloseRequests& closeRequests);
		void OnEvent(Event& event);

		void RegisterCommands(CommandRegistry& commands);

		void ResetVisibility();

	private:
		std::vector<Scope<Panel>> m_Panels;

		// ImGui focuses a window on the frame it appears, and focusing a docked window selects
		// its tab. On the first frame every panel appears at once, so the last one drawn in each
		// dock node would take its front from whatever imgui.ini remembers there. A panel shown
		// later - from a menu, a shortcut - does appear in front, which is what asking for it means.
		bool m_FirstRender = true;
		CommandRegistry* m_Commands = nullptr;
	};
}