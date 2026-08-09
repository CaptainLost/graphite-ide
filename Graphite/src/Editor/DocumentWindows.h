#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::text
{
    class DocumentSet;
}

namespace graphite
{
    /// One ImGui window per open document, docked into the workspace.
    ///
    /// Deliberately not a core::Panel: PanelManager runs Begin/End for each panel and
    /// lists them in the View menu, and a View menu holding thirty open files would be
    /// useless. Rendered from GraphiteLayer immediately after the panels.
    ///
    /// Docked windows give split views for free - drag a tab aside and two files sit side
    /// by side - at the cost of ImGui rather than DocumentSet owning tab order.
    class DocumentWindows
    {
    public:
        explicit DocumentWindows(core::text::DocumentSet& documents) : m_documents(documents) {}

        /// `fallbackDockNode` is where a document goes when no document has been focused
        /// yet this session - typically the workspace's centre node, from
        /// core::DockSpaceHost::CenterNodeId(). Passed in rather than held, since
        /// DocumentWindows knows nothing about DockSpaceHost and GraphiteLayer, which owns
        /// both, is a cheap place to connect them.
        void Render(float fontSize, unsigned int fallbackDockNode);

        /// Opens a file, asking first when it is large enough to need confirmation.
        void RequestOpen(const std::filesystem::path& path, bool permanent);

        /// The workspace root moved on disk and every open document below it follows.
        /// The path is baked into each window's ImGui id, so a rewritten path makes a
        /// window ImGui has never seen - this is the point where that identity change is
        /// handled deliberately, rather than left to happen: the old dock position is
        /// carried over to the new id where possible, and Render's own bookkeeping (see
        /// m_previousNames) purges the abandoned imgui.ini entry.
        void HandleRootMoved(const std::filesystem::path& from, const std::filesystem::path& to);

    private:
        core::text::DocumentSet& m_documents;

        /// The dock node the last focused document sits in. A new window goes there, so a
        /// file opened while working in the right-hand split appears in the right-hand
        /// split. Zero means "no document has been focused yet", in which case Render
        /// falls back to the node it was given.
        unsigned int m_lastFocusedNode = 0;

        /// The file waiting on the size question, and whether it was a double click.
        std::filesystem::path m_pendingPath;
        bool m_pendingPermanent = false;
        bool m_openSizeQuestion = false;

        /// The window names Render produced last time it ran. A document can disappear
        /// between one Render call and the next without going through this window's own
        /// close button - a single click replacing a preview, or a workspace closing and
        /// taking every document with it - and neither of those paths has a chance to call
        /// ImGui::ClearWindowSettings itself. Comparing against this each frame catches
        /// them instead, so imgui.ini does not keep a section for a window nothing opens
        /// again. A window closed through its own tab is purged immediately in the loop
        /// below and simply will not reappear here next frame.
        std::vector<std::string> m_previousNames;

        /// Where a renamed window should be re-docked on the first frame under its new
        /// name, keyed by that new name. Populated by HandleRootMoved from the old
        /// window's dock node, and consumed - erased - the first time that name is seen
        /// in Render.
        std::unordered_map<std::string, unsigned int> m_pendingDockTargets;
    };
}
