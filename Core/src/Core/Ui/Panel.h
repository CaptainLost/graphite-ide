#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <utility>

namespace core
{
    /// Dockable application window. The implementation draws only the interior —
    /// PanelManager is responsible for Begin/End.
    class Panel : NonCopyable
    {
    public:
        Panel(std::string id, std::string title)
            : m_id(std::move(id)), m_title(std::move(title))
        {
        }

        virtual ~Panel() = default;

        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        [[nodiscard]] const std::string& Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Title() const noexcept { return m_title; }

        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        void SetVisible(bool visible) noexcept { m_visible = visible; }

    private:
        std::string m_id;
        std::string m_title;
        bool m_visible = true;
    };
}
