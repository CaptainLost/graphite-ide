#pragma once

#include "Core/Ui/Panel.h"

#include <imgui.h>

#include <string>
#include <utility>

namespace ide
{
    class PlaceholderPanel final : public core::Panel
    {
    public:
        PlaceholderPanel(std::string id, std::string title, std::string message)
            : core::Panel(std::move(id), std::move(title)), m_message(std::move(message))
        {
        }

        void OnUIRender() override
        {
            ImGui::TextDisabled("%s", m_message.c_str());
        }

        void SetMessage(std::string message) { m_message = std::move(message); }

    private:
        std::string m_message;
    };
}
