#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Ui/Panel.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace core
{
    class PanelManager final : NonCopyable
    {
    public:
        /// Returns nullptr if a panel with that identifier already exists.
        Panel* Add(std::unique_ptr<Panel> panel);

        [[nodiscard]] Panel* Find(std::string_view id);
        [[nodiscard]] std::size_t Count() const noexcept { return m_panels.size(); }

        /// Returns false if no panel with the given identifier exists.
        bool SetVisible(std::string_view id, bool visible);

        void OnUpdate(float deltaSeconds);
        void OnUIRender();

        /// Items that toggle visibility — for insertion into the View menu.
        void RenderViewMenuItems();

        void ForEach(const std::function<void(Panel&)>& action);

    private:
        std::vector<std::unique_ptr<Panel>> m_panels;
    };
}
