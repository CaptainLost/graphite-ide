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

        /// Whether this panel is reading raw key presses this frame — a shortcut being
        /// rebound, say. Whoever routes shortcuts has to ask, or the chord the user is
        /// pressing to record it also runs the command it is being taken from. Asked of
        /// every panel rather than of one, so neither the router's caller nor this base
        /// class has to know which panel captures.
        [[nodiscard]] virtual bool IsCapturingInput() const { return false; }

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
