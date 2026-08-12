#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Ui/Panel.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Core
{
    class CommandRegistry;

    class PanelManager final : NonCopyable
    {
    public:
        /// Returns nullptr if a panel with that identifier already exists.
        Panel* Add(std::unique_ptr<Panel> panel);

        [[nodiscard]] Panel* Find(std::string_view id);
        [[nodiscard]] std::size_t GetCount() const noexcept { return m_Panels.size(); }

        /// Whether any panel is reading raw key presses this frame. The caller routes
        /// shortcuts and must not do so while one is: see Panel::IsCapturingInput.
        [[nodiscard]] bool IsCapturingInput() const;

        /// Returns false if no panel with the given identifier exists.
        bool SetVisible(std::string_view id, bool visible);

        void OnUpdate(Timestep ts);
        void OnUIRender();

        /// Registers a visibility toggle per panel, in the group "panels", so a menu can
        /// place them without knowing which panels exist. Panels added afterwards register
        /// as they arrive, so the order of Add and this call does not matter.
        void RegisterCommands(CommandRegistry& commands);

        /// Returns every panel to the visibility it was constructed with.
        void ResetVisibility();

    private:
        std::vector<std::unique_ptr<Panel>> m_Panels;
        CommandRegistry* m_Commands = nullptr;
    };
}
