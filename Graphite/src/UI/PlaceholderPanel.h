#pragma once

#include "Core/ImGui/EmptyState.h"
#include "Core/UI/Panel.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <utility>

namespace Graphite {

	class PlaceholderPanel final : public Core::Panel
	{
	public:
		static constexpr std::string_view kMenuGroup = "tools";
		static constexpr std::string_view kNoIcon = "";

		PlaceholderPanel(std::string id, std::string title, std::string message)
			: Core::Panel(std::move(id), std::move(title), std::string(kMenuGroup), std::string(kNoIcon)),
			  m_Message(std::move(message))
		{
		}

		void OnImGuiRender() override
		{
			Core::EmptyState(m_Message);
		}

		void SetMessage(std::string message) { m_Message = std::move(message); }

	private:
		std::string m_Message;
	};
}