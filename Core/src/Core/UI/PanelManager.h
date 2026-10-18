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
		template<std::derived_from<Panel> PanelType>
		PanelType* Add(Scope<PanelType> panel)
		{
			PanelType* const typedPanel = panel.get();
			const bool wasAdded = AddPanel(std::move(panel)) != nullptr;
			return wasAdded ? typedPanel : nullptr;
		}

		Panel* Find(std::string_view id) const;
		std::size_t GetCount() const { return m_Panels.size(); }

		bool SetVisible(std::string_view id, bool visible);

		bool Toggle(std::string_view id);

		void ForEach(const std::function<void(Panel&)>& visit);

		void OnUpdate(Timestep timeSinceLastFrame);
		void OnImGuiRender(TabCloseRequests& closeRequests);
		void OnEvent(Event& event);

		void RegisterCommands(CommandRegistry& commands);

		void ResetVisibility();

	private:
		Panel* AddPanel(Scope<Panel> panel);

		std::vector<Scope<Panel>> m_Panels;

		// ImGui focuses a window on the frame it appears, which selects its tab in a dock node.
		// On the first frame every panel appears at once, and the last one drawn in each node
		// would push aside the tab imgui.ini remembers there; a panel shown later should come
		// to the front, as asking for it means.
		bool m_FirstRender = true;
		CommandRegistry* m_Commands = nullptr;
	};
}