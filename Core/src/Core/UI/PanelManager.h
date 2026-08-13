#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/UI/Panel.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;

	class PanelManager final : NonCopyable
	{
	public:
		Panel* Add(Scope<Panel> panel);

		Panel* Find(std::string_view id);
		std::size_t GetCount() const { return m_Panels.size(); }

		bool SetVisible(std::string_view id, bool visible);

		void OnUpdate(Timestep ts);
		void OnImGuiRender();
		void OnEvent(Event& event);

		void RegisterCommands(CommandRegistry& commands);

		void ResetVisibility();

	private:
		std::vector<Scope<Panel>> m_Panels;
		CommandRegistry* m_Commands = nullptr;
	};
}