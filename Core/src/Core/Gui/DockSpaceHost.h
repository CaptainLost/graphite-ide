#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <vector>

namespace core
{
    /// Description of the default panel arrangement. Identifiers must match Panel::Id().
    struct DockLayout
    {
        std::vector<std::string> left;
        std::vector<std::string> bottom;
        std::vector<std::string> center;

        float leftRatio = 0.20f;
        float bottomRatio = 0.25f;
    };

    /// Invisible window covering the entire viewport surface, in which the dockspace lives.
    /// Begin must be called before rendering panels, End afterward.
    class DockSpaceHost final : NonCopyable
    {
    public:
        void Begin(const DockLayout& layout);
        void End();

        /// On the next frame, rebuilds the default layout, discarding the current layout
        /// (including one loaded from imgui.ini or modified manually by the user).
        void RequestLayoutReset()
        {
            m_layoutBuilt = false;
            m_forceRebuild = true;
        }

    private:
        bool m_layoutBuilt = false;
        bool m_forceRebuild = false;
    };
}
