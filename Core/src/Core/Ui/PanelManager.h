#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Ui/Panel.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;

	class PanelManager final : NonCopyable
	{
	public:
		Panel* Add(std::unique_ptr<Panel> panel);

		Panel* Find(std::string_view id);
		std::size_t GetCount() const { return m_Panels.size(); }

		bool IsCapturingInput() const;

		bool SetVisible(std::string_view id, bool visible);

		void OnUpdate(Timestep ts);
		void OnUIRender();

		void RegisterCommands(CommandRegistry& commands);

		void ResetVisibility();

	private:
		std::vector<std::unique_ptr<Panel>> m_Panels;
		CommandRegistry* m_Commands = nullptr;
	};
}