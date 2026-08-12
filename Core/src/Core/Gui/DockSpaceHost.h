#pragma once

#include "Core/Base/NonCopyable.h"

#include <string>
#include <vector>

namespace Core
{
    /// Description of the default panel arrangement. Identifiers must match Panel::Id().
    struct DockLayout
    {
        std::vector<std::string> left;
        std::vector<std::string> bottom;
        std::vector<std::string> center;

        float leftRatio = 0.20f;
        float bottomRatio = 0.25f;

        /// Overrides the height of the host window's menu bar row, in pixels — the row
        /// height ImGui would otherwise pick is fixed the instant the host window's
        /// Begin() runs, so DockSpaceHost::Begin is the only place that can still apply
        /// this. Zero leaves ImGui's own FramePadding-derived height alone. Named after
        /// what it does rather than who wants it, so this header stays free of anything
        /// that only Graphite would recognise.
        float menuBarHeight = 0.0f;
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

        /// The id of the node occupying the centre of the layout, for callers that dock
        /// windows of their own once nothing else has told them where to go. Zero before
        /// the first Begin, or if docking somehow has no central node.
        ///
        /// Looked up fresh each call rather than cached from the frame the layout was
        /// built: the node the flag lives on is exactly as valid on a frame where Begin
        /// only restored a layout already in imgui.ini as on the frame that built it.
        [[nodiscard]] unsigned int CenterNodeId() const;

    private:
        bool m_layoutBuilt = false;
        bool m_forceRebuild = false;

        /// The dockspace's own id, recorded every Begin so CenterNodeId() can look up its
        /// central node without recomputing an ImGui ID outside the window scope it was
        /// hashed against - GetID(name) depends on the current ID stack, and by the time a
        /// caller asks for the centre node, Begin/End for the host window has long since
        /// closed.
        unsigned int m_dockSpaceId = 0;
    };
}
