#pragma once

#include "Editor/DocumentWindows.h"
#include "Editor/ModalQuestion.h"
#include "IgnoredDisplay.h"
#include "Workspace.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Event/EventBus.h"
#include "Core/Gui/DockSpaceHost.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Ui/PanelManager.h"

#include <cstddef>
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

        /// Nothing dirty closes immediately. Otherwise asks the "Unsaved changes" question
        /// OnUIRender puts on screen and keeps refusing until the user answers - see
        /// RenderCloseConfirm and FinishClose for how an answer actually reaches the
        /// window.
        bool OnCloseRequested() override;

    private:
        void RenderMenuBar();
        void ApplyIgnoredDisplay(IgnoredDisplay display, bool persist);

        /// What answering the "Unsaved changes" question should do once it resolves in
        /// favour of closing. The exit path and "Close Workspace" both lose whatever is
        /// unsaved - CloseAll no less than the window closing - so both ask through the
        /// same modal and differ only in what FinishClose does at the end.
        enum class ClosePurpose
        {
            ExitApplication,
            CloseWorkspace
        };

        /// Entry point for both close paths. Performs `purpose` immediately when nothing
        /// is dirty; otherwise asks the question RenderCloseConfirm draws and leaves
        /// `purpose` pending until Save All, Discard All or Cancel resolves it.
        void RequestCloseConfirmation(ClosePurpose purpose);

        /// Carries out `purpose` once it is safe to - Save All having succeeded, or
        /// Discard All having been chosen outright. For ExitApplication this has to
        /// re-arm the window's close flag itself: Window::CancelClose, called by
        /// Application after this layer first refused (to put the modal on screen), is
        /// permanent, and nothing re-raises it on its own. See Layer::OnCloseRequested.
        void FinishClose(ClosePurpose purpose);

        /// Draws the "Unsaved changes" modal asked for by RequestCloseConfirmation, listing
        /// every dirty document with Save All / Discard All / Cancel. Called from
        /// OnUIRender at a point that is never nested inside another window's Begin/End,
        /// the same placement DocumentWindows' own modals use and for the same reason:
        /// OpenPopup/BeginPopupModal need to be called there to draw reliably.
        void RenderCloseConfirm();

        [[nodiscard]] bool AnyDocumentModified() const;

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

        /// What Tab, Enter's auto-indent and the newline splitter insert. No menu edits
        /// these yet; they exist so DrawEditor has a real IndentStyle to work with and
        /// so the choice, once a menu sets it, persists the same way the font size does.
        bool m_indentWithSpaces = true;
        std::size_t m_indentWidth = 4;

        /// Set once FinishClose(ExitApplication) has re-armed the window's close flag,
        /// so the OnCloseRequested asked again next frame returns true unconditionally
        /// rather than re-checking for dirty documents. That check matters here: Discard
        /// All does not touch document content, only bypasses asking about it, so every
        /// document FinishClose was told to discard is still modified when this fires.
        bool m_closeApproved = false;

        std::shared_ptr<core::RingBufferSink> m_sink;
        std::filesystem::path m_initialFolder;
        core::EventBus m_events;
        Workspace m_workspace{m_events};
        core::text::DocumentSet m_documents;

        /// Which of the application's five questions currently owns ImGui's single modal
        /// popup level. Declared before m_documentWindows and m_closeQuestion, both of
        /// which take it by reference: four of the five are asked there and the "Unsaved
        /// changes" one here, and all five would otherwise destroy one another. See
        /// ModalGate.
        ModalGate m_modals;
        DocumentWindows m_documentWindows{m_documents, m_modals};

        /// The ImGui popup id of the question below, which is also what the gate records
        /// as the owner of the modal level while it is up. A member of the class rather
        /// than a constant in the .cpp because the question binds to it when this object
        /// is constructed.
        static constexpr const char* kUnsavedWorkTitle = "Unsaved changes##ExitUnsavedWork";

        /// The question asked of the user before anything unsaved is lost, carrying which
        /// close is waiting on the answer. Queued rather than replacing an earlier one,
        /// like every other question here: an exit asked for while a per-tab or reload
        /// question is up goes on screen once that one resolves rather than being dropped.
        /// Cleared the moment the user answers - including Cancel - so a later close
        /// attempt asks fresh rather than remembering the refusal.
        ModalQuestion<ClosePurpose> m_closeQuestion;

        core::PanelManager m_panels;
        core::DockSpaceHost m_dockSpace;
        core::DockLayout m_layout;
        IgnoredDisplay m_ignoredDisplay = IgnoredDisplay::Dim;
    };
}
