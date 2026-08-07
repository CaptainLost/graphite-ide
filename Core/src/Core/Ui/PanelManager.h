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
        /// Zwraca nullptr, jesli panel o takim identyfikatorze juz istnieje.
        Panel* Add(std::unique_ptr<Panel> panel);

        [[nodiscard]] Panel* Find(std::string_view id);
        [[nodiscard]] std::size_t Count() const noexcept { return m_panels.size(); }

        /// Zwraca false, jesli panel o podanym identyfikatorze nie istnieje.
        bool SetVisible(std::string_view id, bool visible);

        void OnUpdate(float deltaSeconds);
        void OnUIRender();

        /// Pozycje przelaczajace widocznosc — do wstawienia w menu View.
        void RenderViewMenuItems();

        void ForEach(const std::function<void(Panel&)>& action);

    private:
        std::vector<std::unique_ptr<Panel>> m_panels;
    };
}
