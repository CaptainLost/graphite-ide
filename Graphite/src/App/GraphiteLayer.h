#pragma once

#include "Editor/DocumentWindows.h"
#include "Editor/EditorSettings.h"
#include "Editor/ModalQuestion.h"
#include "App/TitleBar.h"
#include "Workspace/Workspace.h"

#include "Core/App/Layer.h"
#include "Core/Base/RingBufferSink.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Event/EventBus.h"
#include "Core/Gui/DockSpaceHost.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Ui/PanelManager.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace Graphite
{
    /// The layer that assembles the application: panel registry, dockspace, and menu bar.
    class GraphiteLayer final : public Core::Layer
    {
    public:
        GraphiteLayer(std::shared_ptr<Core::RingBufferSink> sink, std::filesystem::path initialFolder);

        void OnAttach() override;
        void OnUpdate(Core::Timestep ts) override;
        void OnUIRender() override;

        /// Nothing dirty closes immediately. Otherwise asks the "Unsaved changes" question
        /// OnUIRender puts on screen and keeps refusing until the user answers - see
        /// RenderCloseConfirm and FinishClose for how an answer actually reaches the
        /// window.
        bool OnCloseRequested() override;

    private:
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

        /// Reloads the workspace when the window comes back after a pause. Separated
        /// from OnUpdate so its early return governs only itself.
        void UpdateFocusReload(Core::Timestep ts);

        /// How long the application has to have been in the background before coming
        /// back triggers a reload. Alt-tabbing to glance at something and returning is
        /// not a reason to re-read every open directory, and the watcher has been running
        /// the whole time anyway — this is only for the ignore rules it cannot see.
        static constexpr float kReloadAfterSecondsAway = 2.0f;

        /// Whether the window actually gave up its system title bar. Everything the drawn
        /// caption costs — the 32 px row and the padding that centres its labels — is
        /// spent only when this is true, because TitleBar::Render draws nothing when it
        /// is false and the row would then be height paid for nothing, under a system
        /// title bar that is still there.
        bool m_CustomTitleBar = false;

        bool m_AppForeground = true;
        float m_SecondsInBackground = 0.0f;

        /// Set once FinishClose(ExitApplication) has re-armed the window's close flag,
        /// so the OnCloseRequested asked again next frame returns true unconditionally
        /// rather than re-checking for dirty documents. That check matters here: Discard
        /// All does not touch document content, only bypasses asking about it, so every
        /// document FinishClose was told to discard is still modified when this fires.
        bool m_CloseApproved = false;

        std::shared_ptr<Core::RingBufferSink> m_Sink;
        std::filesystem::path m_InitialFolder;
        Core::EventBus m_Events;
        Workspace m_Workspace{m_Events};
        Core::DocumentSet m_Documents;

        /// Which of the application's five questions currently owns ImGui's single modal
        /// popup level. Declared before m_DocumentWindows and m_CloseQuestion, both of
        /// which take it by reference: four of the five are asked there and the "Unsaved
        /// changes" one here, and all five would otherwise destroy one another. See
        /// ModalGate.
        ModalGate m_Modals;
        DocumentWindows m_DocumentWindows{m_Documents, m_Modals};

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
        ModalQuestion<ClosePurpose> m_CloseQuestion;

        Core::PanelManager m_Panels;
        Core::DockSpaceHost m_DockSpace;
        Core::DockLayout m_Layout;
        TitleBar m_TitleBar;

        Core::CommandRegistry m_Commands;
        Core::Keymap m_Keys;
        Core::ShortcutRouter m_Router;
        Core::MenuBar m_MenuBar;

        /// Built once in OnAttach, after every command is registered — the tree names
        /// ids, and a tree built before them would draw a bar full of unregistered
        /// entries.
        std::vector<Core::MenuItem> m_MenuTree;

        EditorSettings m_EditorSettings;
    };
}
