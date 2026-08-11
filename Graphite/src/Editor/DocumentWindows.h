#pragma once

#include "Editor/ModalGate.h"
#include "Editor/ModalQuestion.h"

#include "Core/Text/Editing.h"
#include "Core/Text/FileSync.h"

#include <cstdint>
#include <filesystem>
#include <optional>
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
        /// `modals` is the application-wide gate deciding which question may be on screen.
        /// Four of the five modals in the application are drawn here and the fifth
        /// (GraphiteLayer's "Unsaved changes" exit question) elsewhere, and every one of
        /// them opens at ImGui's popup level 0, where a second OpenPopup destroys the
        /// first - so they all consult the same gate rather than each knowing about the
        /// others. See ModalGate.
        DocumentWindows(core::text::DocumentSet& documents, ModalGate& modals)
            : m_documents(documents), m_modals(modals),
              m_sizeQuestion(modals, kSizeQuestionTitle),
              m_reloadConflict(modals, kReloadConflictTitle),
              m_overwriteConfirm(modals, kOverwriteConfirmTitle),
              m_closeConfirm(modals, kCloseConfirmTitle)
        {
        }

        /// `fallbackDockNode` is where a document goes when no document has been focused
        /// yet this session - typically the workspace's centre node, from
        /// core::DockSpaceHost::CenterNodeId(). Passed in rather than held, since
        /// DocumentWindows knows nothing about DockSpaceHost and GraphiteLayer, which owns
        /// both, is a cheap place to connect them.
        ///
        /// `indentStyle` governs what Tab, Enter's auto-indent and the newline splitter
        /// insert, in whichever document has keyboard focus.
        ///
        /// Returns the total Ctrl+wheel zoom delta applied across every visible document
        /// this frame - only one can be hovered at a time, so in practice at most one
        /// contributes, but summing costs nothing and needs no assumption about that.
        float Render(float fontSize, core::text::IndentStyle indentStyle,
                    unsigned int fallbackDockNode);

        /// Opens a file, asking first when it is large enough to need confirmation.
        void RequestOpen(const std::filesystem::path& path, bool permanent);

        /// The workspace root moved on disk and every open document below it follows.
        /// The path is baked into each window's ImGui id, so a rewritten path makes a
        /// window ImGui has never seen - this is the point where that identity change is
        /// handled deliberately, rather than left to happen: the old dock position is
        /// carried over to the new id where possible, and Render's own bookkeeping (see
        /// m_previousIdentities) purges the abandoned imgui.ini entry.
        void HandleRootMoved(const std::filesystem::path& from, const std::filesystem::path& to);

        /// Files changed under one or more directories - or, with `everything`, somewhere
        /// the watcher could not name. Any open document whose file has since been deleted
        /// is marked missing, so a tab whose file was removed in Explorer stops claiming
        /// the file is still there.
        ///
        /// The reconciling itself is core::text::SyncWithFile, one item at a time; this
        /// decides which items are worth reconciling and what to do about what it found.
        ///
        /// The check is a stat per open document sitting in one of those directories,
        /// which is why it hangs off a change notification rather than running per frame:
        /// nothing happens on the frames where nothing happened on disk.
        void HandleFilesChanged(const std::vector<std::filesystem::path>& directories,
                                bool everything);

        /// Runs the same reconciliation as HandleFilesChanged, over every open document
        /// rather than only the ones under directories a watcher named, when `focusedNow`
        /// marks the frame the OS window regains focus after having lost it (compared
        /// against the previous call's value, held here). Watchers drop events - buffer
        /// overflow, or a tool touching many files at once - so this is the safety net for
        /// the moment that most plausibly matters: coming back to the editor from
        /// somewhere else. Costs one stat per open document, which is why it only runs on
        /// the transition rather than every frame.
        ///
        /// `focusedNow` is glfwGetWindowAttrib(handle, GLFW_FOCUSED) for the main window,
        /// read by the caller - DocumentWindows knows nothing about GLFW.
        void NotifyWindowFocus(bool focusedNow);

        /// core::text::SaveAllModified plus the one status bar message describing what it
        /// did. The two rules - a conflicted document is skipped and the batch continues,
        /// a genuine write failure stops it - live there, where they can be tested; the
        /// English lives here, where there is a status bar to put it in.
        ///
        /// Every skipped document is named together in that one message alongside how many
        /// saved cleanly, so "saved" never quietly means "saved except this one". A
        /// stopped batch says instead why the write failed and which document it was, and
        /// no summary is added over the top of it, which would only bury the reason.
        ///
        /// Returns true only when every dirty document ended up saved - false for either
        /// a stopped batch or one that finished with documents skipped over a conflict.
        /// Ctrl+Shift+S goes through this; so, in the next stage, does the exit question,
        /// which is why this is public rather than folded into Render() - one save-all
        /// recipe, not two.
        bool SaveAllModified();

        /// A short-lived note for the status bar - what a reload or a failed save just
        /// did - or nothing once it has expired. GraphiteLayer reads this every frame to
        /// feed DrawStatusBar.
        [[nodiscard]] std::optional<std::string> StatusMessage() const;

    private:
        /// Turns what core::text::SyncWithFile found about one item into the question or
        /// the status bar note this application shows for it, and nothing else - the
        /// reconciling itself already happened. The outcome is passed in rather than
        /// fetched here so that the two callers keep deciding when to sync, which is the
        /// only part of this they disagree about.
        void RespondToSync(std::size_t index, core::text::FileSyncOutcome outcome);

        /// core::text::ReloadItem plus a status bar trace: what was reloaded, or what
        /// stopped it. On failure nothing about the document changes; the trace names the
        /// error instead.
        void ReloadOne(std::size_t index);

        /// core::text::SaveDocument plus a status bar trace either way. Returns whether it
        /// succeeded, which is what lets a caller stop at the first failure (the close-tab
        /// question). Stops neither to ask nor to check ChangedOnDisk - that is
        /// RequestSave's job.
        bool SaveOne(std::size_t index);

        /// Ctrl+S over one document: asks first when ChangedOnDisk() says the write would
        /// clobber a change nobody was told about, otherwise saves immediately.
        void RequestSave(std::size_t index);

        void SetTransientMessage(std::string text);

        /// The ImGui popup id of each question asked here, which is also what the gate
        /// records as the owner of the modal level while that question is up. Members of
        /// the class rather than constants in the .cpp because the questions below bind to
        /// them where they are constructed, which is in this header.
        static constexpr const char* kSizeQuestionTitle = "Large file##EditorSizeQuestion";
        static constexpr const char* kReloadConflictTitle =
            "File changed on disk##EditorReloadConflict";
        static constexpr const char* kOverwriteConfirmTitle =
            "Overwrite unsaved changes on disk?##EditorOverwriteConfirm";
        static constexpr const char* kCloseConfirmTitle = "Unsaved changes##EditorCloseConfirm";

        /// What one asking of the size question is about: which file, whether the click
        /// that asked for it was a double click (a permanent tab rather than a preview),
        /// and the file's size in bytes, measured once when the question was raised. Zero
        /// means it could not be measured, which the modal says rather than asking about
        /// "0 MB".
        struct LargeFileQuestion
        {
            std::filesystem::path path;
            bool permanent = false;
            std::uint64_t size = 0;
        };

        core::text::DocumentSet& m_documents;

        /// Who currently owns the one modal level every question in this application
        /// competes for. Consulted by each of the four questions below before it opens,
        /// and released the moment one is answered - or found to have vanished. Declared
        /// before them, since each holds a reference to it.
        ModalGate& m_modals;

        /// The dock node the last focused document sits in. A new window goes there, so a
        /// file opened while working in the right-hand split appears in the right-hand
        /// split. Zero means "no document has been focused yet", in which case Render
        /// falls back to the node it was given.
        unsigned int m_lastFocusedNode = 0;

        /// Files big enough that opening one is worth confirming first. A question raised
        /// while the gate is held by another is asked once that one resolves rather than
        /// dropped, and a second large file picked while the first question is still up is
        /// asked about after it rather than replacing it.
        ModalQuestion<LargeFileQuestion> m_sizeQuestion;

        /// The identity (core::fs::ToUtf8 of the path, nothing else) of every window
        /// Render produced last time it ran. A document can disappear between one Render
        /// call and the next without going through this window's own close button - a
        /// single click replacing a preview, or a workspace closing and taking every
        /// document with it - and neither of those paths has a chance to call
        /// ImGui::ClearWindowSettings itself. Comparing against this each frame catches
        /// them instead, so imgui.ini does not keep a section for a window nothing opens
        /// again. A window closed through its own tab is purged immediately in the loop
        /// below and simply will not reappear here next frame.
        ///
        /// Keyed on identity rather than the full "DisplayTitle###path" window name on
        /// purpose: DisplayTitle changes whenever another open item starts or stops
        /// sharing its file name, which would make a document that never closed look
        /// "gone" to a name-based diff - and ImGui::ClearWindowSettings would then find
        /// and undock the still-open window it renamed *into*, because ImHashStr resets
        /// at "###" and hashes only what follows it, same as ImGui's own window identity.
        std::vector<std::string> m_previousIdentities;

        /// Where a renamed window should be re-docked on the first frame under its new
        /// identity, keyed by that new identity (the same core::fs::ToUtf8 path used in
        /// m_previousIdentities, not the rendered name - the same reasoning applies: the
        /// display title either side of a rename need not match what was true when the
        /// rename happened). Populated by HandleRootMoved from the old window's dock node,
        /// and consumed - erased - the first time that identity is seen in Render.
        std::unordered_map<std::string, unsigned int> m_pendingDockTargets;

        /// Whether the OS window had focus as of the last NotifyWindowFocus call. Starts
        /// true: the application is assumed foreground from its first frame, the same
        /// assumption GraphiteLayer::m_appForeground makes, and false here would fire a
        /// needless sweep before anything has even been drawn once.
        bool m_wasFocused = true;

        /// Documents whose file changed on disk while the tab was dirty - what
        /// core::text::SyncWithFile reports as NeedsDecision. RespondToSync asks, so
        /// several dirty files changing at once (a git checkout) are asked about one at a
        /// time instead of fighting over the same modal.
        ///
        /// A path rather than an index: the modal can outlive several frames, during which
        /// closing an earlier tab would shift every index after it. Looked up again with
        /// DocumentSet::Find when the user answers, and simply dropped - no crash, no
        /// stale write - if the document is gone by then.
        ModalQuestion<std::filesystem::path> m_reloadConflict;

        /// Documents RequestSave has to confirm an overwrite for - ChangedOnDisk() was
        /// true right when Ctrl+S was pressed, meaning a notification was dropped and this
        /// is the last chance to ask. Same "path, not index" reasoning as m_reloadConflict.
        ModalQuestion<std::filesystem::path> m_overwriteConfirm;

        /// The document the next Render should bring to the front, or nullopt. Set when
        /// Open() reports Activated - a file the user picked in the tree that was already
        /// open - because that path changes nothing ImGui can see: DocumentSet::Activate
        /// moves m_active, and focus otherwise only ever flows the other way, from
        /// ImGui::IsWindowFocused into the set. A file that was *not* already open needs
        /// nothing here, since a window ImGui has just seen for the first time takes focus
        /// by itself; that is why the gap only ever showed on reopening.
        ///
        /// A path rather than an index, as elsewhere here: the request is made outside
        /// Render, and evicting a preview shifts every index after it. Taken by Render
        /// before its loop and cleared in the same breath, so it lives exactly one frame -
        /// a request naming a document that has since gone simply matches nothing and is
        /// dropped, rather than lingering to steal focus whenever that path is next opened.
        std::optional<std::filesystem::path> m_focusRequest;

        /// Documents whose own close button was pressed while they were still dirty. A
        /// second tab's close button pressed before the first question resolves still gets
        /// its own question, in order, rather than being silently dropped. Same "path, not
        /// index" reasoning as m_reloadConflict.
        ///
        /// This is the one question with something to check at the moment it is taken off
        /// the queue rather than only when it was raised - see the predicate in Render.
        ModalQuestion<std::filesystem::path> m_closeConfirm;

        /// A short-lived status bar note and when it expires, both on ImGui::GetTime()'s
        /// clock. Read through StatusMessage().
        struct TransientMessage
        {
            std::string text;
            double expiresAt = 0.0;
        };

        std::optional<TransientMessage> m_transientMessage;
    };
}
