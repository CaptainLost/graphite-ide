#pragma once

#include "Editor/DocumentWindows.h"
#include "IgnoredDisplay.h"
#include "Workspace.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Event/EventBus.h"
#include "Core/Gui/DockSpaceHost.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Ui/PanelManager.h"

#include <filesystem>
#include <memory>

namespace graphite
{
    /// The layer that assembles the application: panel registry, dockspace, and menu bar.
    class GraphiteLayer final : public core::Layer
    {
    public:
        GraphiteLayer(std::shared_ptr<core::RingBufferSink> sink, std::filesystem::path initialFolder);

        void OnAttach() override;
        void OnUpdate(float deltaSeconds) override;
        void OnUIRender() override;

    private:
        void RenderMenuBar();
        void ApplyIgnoredDisplay(IgnoredDisplay display, bool persist);

        /// Clamps `newSize` into range, applies it and persists it — the single path
        /// both the Ctrl+wheel handling and the View menu's zoom items go through, so
        /// they cannot drift apart on what "zoom in" or "reset" means.
        void SetEditorFontSize(float newSize);

        /// Reloads the workspace when the window comes back after a pause. Separated
        /// from OnUpdate so its early return governs only itself.
        void UpdateFocusReload(float deltaSeconds);

        /// How long the application has to have been in the background before coming
        /// back triggers a reload. Alt-tabbing to glance at something and returning is
        /// not a reason to re-read every open directory, and the watcher has been running
        /// the whole time anyway — this is only for the ignore rules it cannot see.
        static constexpr float kReloadAfterSecondsAway = 2.0f;

        bool m_appForeground = true;
        float m_secondsInBackground = 0.0f;

        /// Editor font size in pixels. Separate from the interface size, so zooming the
        /// code leaves the menus alone.
        float m_editorFontSize = 13.0f;

        static constexpr float kMinimumEditorFontSize = 8.0f;
        static constexpr float kMaximumEditorFontSize = 32.0f;
        static constexpr float kDefaultEditorFontSize = 13.0f;

        /// The change applied by one click of the View menu's "Zoom In"/"Zoom Out" —
        /// there is no wheel delta to read from a menu click, so this stands in for one.
        static constexpr float kEditorFontSizeStep = 1.0f;

        std::shared_ptr<core::RingBufferSink> m_sink;
        std::filesystem::path m_initialFolder;
        core::EventBus m_events;
        Workspace m_workspace{m_events};
        core::text::DocumentSet m_documents;
        DocumentWindows m_documentWindows{m_documents};
        core::PanelManager m_panels;
        core::DockSpaceHost m_dockSpace;
        core::DockLayout m_layout;
        IgnoredDisplay m_ignoredDisplay = IgnoredDisplay::Dim;
    };
}
