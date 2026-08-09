#pragma once

#include "Core/Text/Editing.h"

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
        explicit DocumentWindows(core::text::DocumentSet& documents) : m_documents(documents) {}

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

        /// Saves every modified document. A document whose ChangedOnDisk() is true is
        /// skipped rather than stopping the batch - one stale file among five dirty ones
        /// is no reason to leave the other four unsaved - and never silently overwritten
        /// either: asking about each conflict in turn here would be the modal storm the
        /// queued reload-conflict question exists to avoid, so a conflict only ever gets
        /// its Overwrite/Cancel question from a deliberate per-document Ctrl+S
        /// (RequestSave). Every skipped document is named together in one status bar
        /// message alongside how many saved cleanly, so "saved" never quietly means
        /// "saved except this one".
        ///
        /// A genuine write failure is different: it stops the batch on the spot, since a
        /// batch that keeps going past a real error is a batch pretending nothing went
        /// wrong. The failing document stays dirty with its undo history intact and
        /// SaveOne's own reason - naming that document specifically - is left as the
        /// status bar message.
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
        /// Reconciles one open item against disk: an existence check, exactly as
        /// HandleFilesChanged always ran, plus (added for saving and reconciliation) the
        /// present-again case for an item marked Missing, and the changed-on-disk case
        /// for one that carries a document. A stat error is never treated as an answer -
        /// see HandleFilesChanged's own comment, which this now shares by delegation
        /// rather than duplicating.
        void CheckItem(std::size_t index);

        /// The clean-document half of step 3: reload, clamp the caret to the (possibly
        /// shorter) new content, rebuild the caches, and leave a status bar trace. On
        /// failure nothing about the document changes; the trace names the error instead.
        void ReloadClean(std::size_t index);

        /// Writes the document, stopping neither to ask nor to check ChangedOnDisk -
        /// that is RequestSave's job. Leaves a status bar trace either way. Returns
        /// whether it succeeded, which is what lets a caller stop a batch (Ctrl+Shift+S,
        /// the close-tab question) at the first failure.
        bool SaveOne(std::size_t index);

        /// Ctrl+S over one document: asks first when ChangedOnDisk() says the write would
        /// clobber a change nobody was told about, otherwise saves immediately.
        void RequestSave(std::size_t index);

        void SetTransientMessage(std::string text);

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

        /// The size the question is about, in bytes, measured once when the question was
        /// raised. Zero means it could not be measured, which the modal says rather than
        /// asking about "0 MB".
        std::uint64_t m_pendingSize = 0;

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

        /// Documents waiting to be asked about a reload conflict - ChangedOnDisk() true
        /// on a dirty document - oldest first. CheckItem appends; Render pops the front
        /// into m_reloadConflictActive as soon as no conflict is on screen, so several
        /// dirty files changing at once (a git checkout) ask one at a time instead of
        /// fighting over the same modal.
        std::vector<std::filesystem::path> m_reloadConflictQueue;

        /// The document the reload-conflict modal is currently asking about, or nullopt
        /// when none is showing. A path rather than an index: the modal can outlive
        /// several frames, during which closing an earlier tab would shift every index
        /// after it. Looked up again with DocumentSet::Find when the user answers, and
        /// simply dropped - no crash, no stale write - if the document is gone by then.
        std::optional<std::filesystem::path> m_reloadConflictActive;
        bool m_openReloadConflict = false;

        /// The document RequestSave is asking to confirm an overwrite for - ChangedOnDisk()
        /// was true right when Ctrl+S was pressed, meaning a notification was dropped and
        /// this is the last chance to ask. Same "path, not index" reasoning as
        /// m_reloadConflictActive.
        std::optional<std::filesystem::path> m_overwriteConfirmPath;
        bool m_openOverwriteConfirm = false;

        /// Documents whose own close button was pressed while they were still dirty,
        /// oldest first - the same queue-of-paths shape as m_reloadConflictQueue and for
        /// the same reason: a second tab's close button pressed before the first
        /// question resolves must still get its own question, in order, rather than
        /// being silently dropped.
        std::vector<std::filesystem::path> m_pendingCloseQueue;

        /// The document the close-confirm modal is currently asking about, or nullopt
        /// when none is showing. Same "path, not index" reasoning as
        /// m_reloadConflictActive.
        std::optional<std::filesystem::path> m_pendingCloseActive;
        bool m_openCloseConfirm = false;

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
