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
        Panel(std::string id, std::string title, bool visible = true)
            : m_id(std::move(id)), m_title(std::move(title)), m_visible(visible),
              m_defaultVisible(visible)
        {
        }

        virtual ~Panel() = default;

        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        [[nodiscard]] const std::string& Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Title() const noexcept { return m_title; }

        [[nodiscard]] bool IsVisible() const noexcept { return m_visible; }
        void SetVisible(bool visible) noexcept { m_visible = visible; }

        /// What this panel's visibility returns to when the layout is reset. A panel that
        /// starts hidden stays hidden through a reset: nobody asked to open it.
        [[nodiscard]] bool DefaultVisible() const noexcept { return m_defaultVisible; }

    private:
        std::string m_id;
        std::string m_title;
        bool m_visible = true;
        bool m_defaultVisible = true;
    };
}
