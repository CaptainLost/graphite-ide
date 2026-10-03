#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/UI/Panel.h"

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Core {

	class CommandRegistry;
	class TabCloseRequests;

	class PanelManager final : NonCopyable
	{
	public:
		// Takes the panel and hands it back as the type it was given, so an owner that needs to
		// reach it later keeps a typed pointer instead of casting the base one down. Null when a
		// panel with the same id is already here, and the new one is dropped.
		template<std::derived_from<Panel> T>
		T* Add(Scope<T> panel)
		{
			T* const typed = panel.get();
			return AddPanel(std::move(panel)) != nullptr ? typed : nullptr;
		}

		Panel* Find(std::string_view id) const;
		std::size_t GetCount() const { return m_Panels.size(); }

		bool SetVisible(std::string_view id, bool visible);

		void ForEach(const std::function<void(Panel&)>& visit);

		void OnUpdate(Timestep ts);
		void OnImGuiRender(TabCloseRequests& closeRequests);
		void OnEvent(Event& event);

		void RegisterCommands(CommandRegistry& commands);

		void ResetVisibility();

	private:
		Panel* AddPanel(Scope<Panel> panel);

		std::vector<Scope<Panel>> m_Panels;

		// ImGui focuses a window on the frame it appears, and focusing a docked window selects
		// its tab. On the first frame every panel appears at once, so the last one drawn in each
		// dock node would take its front from whatever imgui.ini remembers there. A panel shown
		// later - from a menu, a shortcut - does appear in front, which is what asking for it means.
		bool m_FirstRender = true;
		CommandRegistry* m_Commands = nullptr;
	};
}