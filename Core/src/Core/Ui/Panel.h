#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <utility>

namespace Core
{
    /// Dockable application window. The implementation draws only the interior —
    /// PanelManager is responsible for Begin/End.
    class Panel : NonCopyable
    {
    public:
        Panel(std::string id, std::string title, bool visible = true)
            : m_Id(std::move(id)), m_Title(std::move(title)), m_Visible(visible),
              m_DefaultVisible(visible)
        {
        }

        virtual ~Panel() = default;

        virtual void OnUpdate(float deltaSeconds) { (void)deltaSeconds; }
        virtual void OnUIRender() {}

        /// Called instead of OnUIRender on a frame where the body did not run — hidden,
        /// collapsed, or not the selected tab of its dock node. Anything a panel can only
        /// finish while drawing has to end here, or it outlives the frame with nothing on
        /// screen to explain it: a shortcut capture left waiting behind a document tab
        /// would go on swallowing every shortcut in the application.
        virtual void OnNotDrawn() {}

        /// Whether this panel is reading raw key presses this frame — a shortcut being
        /// rebound, say. Whoever routes shortcuts has to ask, or the chord the user is
        /// pressing to record it also runs the command it is being taken from. Asked of
        /// every panel rather than of one, so neither the router's caller nor this base
        /// class has to know which panel captures.
        [[nodiscard]] virtual bool IsCapturingInput() const { return false; }

        [[nodiscard]] const std::string& Id() const noexcept { return m_Id; }
        [[nodiscard]] const std::string& Title() const noexcept { return m_Title; }

        [[nodiscard]] bool IsVisible() const noexcept { return m_Visible; }
        void SetVisible(bool visible) noexcept { m_Visible = visible; }

        /// What this panel's visibility returns to when the layout is reset. A panel that
        /// starts hidden stays hidden through a reset: nobody asked to open it.
        [[nodiscard]] bool DefaultVisible() const noexcept { return m_DefaultVisible; }

    private:
        std::string m_Id;
        std::string m_Title;
        bool m_Visible = true;
        bool m_DefaultVisible = true;
    };
}
