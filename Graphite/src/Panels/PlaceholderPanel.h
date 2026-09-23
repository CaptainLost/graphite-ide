#pragma once

#include "Core/UI/Panel.h"

#include <imgui.h>

#include <string>
#include <utility>

namespace Graphite {

	class PlaceholderPanel final : public Core::Panel
	{
	public:
		PlaceholderPanel(std::string id, std::string title, std::string message)
			: Core::Panel(std::move(id), std::move(title), "tools", ""), m_Message(std::move(message))
		{
		}

		void OnImGuiRender() override
		{
			ImGui::TextDisabled("%s", m_Message.c_str());
		}

		void SetMessage(std::string message) { m_Message = std::move(message); }

	private:
		std::string m_Message;
	};
}