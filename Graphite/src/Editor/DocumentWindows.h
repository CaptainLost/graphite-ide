#pragma once

#include <filesystem>

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

        void Render(float fontSize);

        /// Opens a file, asking first when it is large enough to need confirmation.
        void RequestOpen(const std::filesystem::path& path, bool permanent);

    private:
        core::text::DocumentSet& m_documents;

        /// The dock node the last focused document sits in. A new window goes there, so a
        /// file opened while working in the right-hand split appears in the right-hand
        /// split. Zero means "no document has been focused yet" - use the centre.
        unsigned int m_lastFocusedNode = 0;

        /// The file waiting on the size question, and whether it was a double click.
        std::filesystem::path m_pendingPath;
        bool m_pendingPermanent = false;
        bool m_openSizeQuestion = false;
    };
}
