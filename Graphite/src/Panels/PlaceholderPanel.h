#pragma once

#include "Core/Ui/Panel.h"

#include <imgui.h>

#include <string>
#include <utility>

namespace Graphite {

	class PlaceholderPanel final : public Core::Panel
	{
	public:
		PlaceholderPanel(std::string id, std::string title, std::string message)
			: Core::Panel(std::move(id), std::move(title)), m_Message(std::move(message))
		{
		}

		void OnUIRender() override
		{
			ImGui::TextDisabled("%s", m_Message.c_str());
		}

		void SetMessage(std::string message) { m_Message = std::move(message); }

	private:
		std::string m_Message;
	};
}