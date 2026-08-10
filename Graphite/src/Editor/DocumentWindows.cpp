#include "Editor/DocumentWindows.h"

#include "Editor/TextEditorWidget.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Gui/Icons.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

namespace graphite
{
    namespace
    {
        constexpr const char* kSizeQuestionTitle = "Large file##EditorSizeQuestion";
        constexpr const char* kReloadConflictTitle = "File changed on disk##EditorReloadConflict";
        constexpr const char* kOverwriteConfirmTitle =
            "Overwrite unsaved changes on disk?##EditorOverwriteConfirm";
        constexpr const char* kCloseConfirmTitle = "Unsaved changes##EditorCloseConfirm";

        /// How long a status bar note stays up once set.
        constexpr double kTransientMessageSeconds = 4.0;

        /// "a, b, c" - how SaveAllModified names the documents it skipped, together
        /// rather than one status message per file overwriting the last.
        [[nodiscard]] std::string Join(const std::vector<std::string>& values)
        {
            std::string joined;

            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index > 0)
                    joined += ", ";

                joined += values[index];
            }

            return joined;
        }

        [[nodiscard]] std::string WindowName(const core::text::DocumentSet& documents,
                                             std::size_t index)
        {
            // ImGui shows what precedes ### and identifies by what follows, so the path -
            // the only identity that cannot collide - stays out of sight. That is also what
            // makes the icon free to add: everything before ### is decoration, and no
            // window id, dock position or imgui.ini entry moves because of it.
            //
            // The same IconForExtension the explorer rows use, so one file reads the same
            // in the tree and on its tab. Drawn from the regular mono face, which is the
            // only one carrying the MDI glyphs - nothing here may push another.
            const std::filesystem::path& path = documents.At(index).path;

            return std::string{core::IconForExtension(core::fs::ToUtf8(path.extension()))} + "  "
                   + documents.DisplayTitle(index) + "###" + core::fs::ToUtf8(path);
        }

        /// The part of WindowName that is actually the window's identity as far as ImGui
        /// is concerned: ImHashStr resets its running hash at "###" and hashes only what
        /// follows, so "anything###path" and "path" alone hash identically. Bookkeeping
        /// that has to survive a display title changing underneath it - two open items
        /// starting or stopping sharing a file name changes DisplayTitle for both, without
        /// either one closing - has to be keyed on this, not on WindowName's result.
        [[nodiscard]] std::string Identity(const core::text::DocumentSet& documents, std::size_t index)
        {
            return core::fs::ToUtf8(documents.At(index).path);
        }

        [[nodiscard]] std::vector<std::string> CurrentIdentities(const core::text::DocumentSet& documents)
        {
            std::vector<std::string> identities;
            identities.reserve(documents.Count());

            for (std::size_t index = 0; index < documents.Count(); ++index)
                identities.push_back(Identity(documents, index));

            return identities;
        }
    }

    void DocumentWindows::RequestOpen(const std::filesystem::path& path, bool permanent)
    {
        const core::text::OpenOutcome outcome = m_documents.Open(path, permanent);

        // Already open: the set has activated it, but nothing about that reaches ImGui,
        // which goes on showing whichever tab it last focused - so ask Render to bring
        // this one forward. See m_focusRequest.
        if (outcome == core::text::OpenOutcome::Activated)
            m_focusRequest = path;

        if (outcome != core::text::OpenOutcome::NeedsConfirmation)
            return;

        m_pendingPath = path;
        m_pendingPermanent = permanent;
        m_openSizeQuestion = true;
        m_pendingSize = 0;

        // Probed once, here, rather than every frame the modal is open: a stat plus an
        // 8 KB read per frame is real work when the file sits on a network share, and the
        // answer cannot change between the question and the reply in any way worth
        // redrawing for. Open() has just probed the same file successfully - that is what
        // NeedsConfirmation means - so this second probe is asking a question already
        // answered, and a failure here means the file went away in between.
        if (const auto probe = core::text::ProbeFile(path))
            m_pendingSize = probe.Value().size;
    }

    void DocumentWindows::HandleRootMoved(const std::filesystem::path& from,
                                          const std::filesystem::path& to)
    {
        const std::vector<std::string> oldIdentities = CurrentIdentities(m_documents);

        m_documents.RewritePaths(from, to);

        const std::vector<std::string> newIdentities = CurrentIdentities(m_documents);

        // RewritePaths only ever changes paths in place, never the count or the order, so
        // the two snapshots line up index for index.
        for (std::size_t index = 0; index < newIdentities.size(); ++index)
        {
            const std::string& oldIdentity = oldIdentities[index];
            const std::string& newIdentity = newIdentities[index];

            if (newIdentity == oldIdentity)
                continue;

            unsigned int target = 0;

            // The live window, if this document was ever drawn under its old identity, is
            // authoritative for where it currently sits.
            if (ImGuiWindow* oldWindow = ImGui::FindWindowByName(oldIdentity.c_str());
                oldWindow != nullptr && oldWindow->DockNode != nullptr)
            {
                target = oldWindow->DockNode->ID;
            }
            else if (const auto pending = m_pendingDockTargets.find(oldIdentity);
                     pending != m_pendingDockTargets.end())
            {
                // The root moved again before a Render ever consumed the previous move's
                // target - chain it forward instead of leaving a dead entry keyed on an
                // identity nothing will ever look up again.
                target = pending->second;
            }

            m_pendingDockTargets.erase(oldIdentity);

            if (target != 0)
                m_pendingDockTargets[newIdentity] = target;
        }
    }

    void DocumentWindows::HandleFilesChanged(
        const std::vector<std::filesystem::path>& directories, bool everything)
    {
        for (std::size_t index = 0; index < m_documents.Count(); ++index)
        {
            const core::text::OpenItem& item = m_documents.At(index);

            // The watcher names directories, so an item is worth a stat only when its own
            // directory is one of them. `everything` means the batch could not be narrowed
            // down at all - dropped notifications, or a git state change - and every open
            // document has to be checked. Items already marked Missing are stated too now
            // (see CheckItem): the file may have come back.
            if (!everything
                && std::find(directories.begin(), directories.end(), item.path.parent_path())
                       == directories.end())
                continue;

            CheckItem(index);
        }
    }

    void DocumentWindows::NotifyWindowFocus(bool focusedNow)
    {
        const bool regained = focusedNow && !m_wasFocused;
        m_wasFocused = focusedNow;

        if (!regained)
            return;

        for (std::size_t index = 0; index < m_documents.Count(); ++index)
            CheckItem(index);
    }

    void DocumentWindows::CheckItem(std::size_t index)
    {
        core::text::OpenItem& item = m_documents.At(index);

        std::error_code error;
        const bool present = std::filesystem::exists(item.path, error);

        if (item.failure == core::text::OpenFailure::Missing)
        {
            // An error is not an answer, the same as below: a share that blinked must not
            // read as "still gone".
            if (error || !present)
                return;

            m_documents.MarkPresent(item.path);

            // Deliberately falls through rather than returning. This is the very event
            // saying the file came back, and so the one moment its content is most worth
            // comparing: a file deleted and restored with different content - git checkout
            // after a git rm - would otherwise go on showing the old content until some
            // unrelated later sweep happened to notice. MarkPresent only clears the
            // Missing flag; it does not touch the item's document or its stamp, and the
            // reference above stays valid across it.
        }

        // An error is not an answer. A share that blinked or a directory that briefly
        // denied access must not turn every document open on it into a deleted one - the
        // same reasoning SyncRepository applies before dropping a repository.
        if (!error && !present)
        {
            m_documents.MarkMissing(item.path);
            return;
        }

        if (!item.document || !item.document->ChangedOnDisk())
            return;

        if (!item.document->IsModified())
        {
            ReloadClean(index);
            return;
        }

        // Dirty: queue the question rather than asking immediately, so several files
        // changing in the same batch (a git checkout) are asked about one at a time
        // instead of every modal fighting to open this frame.
        const std::filesystem::path& path = item.path;

        if (m_reloadConflictActive == path)
            return;

        if (std::find(m_reloadConflictQueue.begin(), m_reloadConflictQueue.end(), path)
            != m_reloadConflictQueue.end())
            return;

        m_reloadConflictQueue.push_back(path);
    }

    void DocumentWindows::ReloadClean(std::size_t index)
    {
        core::text::OpenItem& item = m_documents.At(index);
        const std::string fileName = core::fs::ToUtf8(item.path.filename());

        if (auto result = item.document->ReloadFromDisk())
        {
            // The reload can shorten the document past where the caret sat. PlaceAt
            // clamps to the new length and pulls back to a character boundary, the same
            // as a click past a line's end does.
            item.caret = core::text::PlaceAt(item.document->Buffer(), item.caret,
                                             item.caret.offset, false,
                                             core::text::DefaultTabWidth);
            core::text::RefreshDerived(item);
            SetTransientMessage(fileName + " reloaded (changed on disk)");
        }
        else
        {
            SetTransientMessage("Could not reload " + fileName + ": " + result.GetError().message);
        }
    }

    bool DocumentWindows::SaveOne(std::size_t index)
    {
        core::text::OpenItem& item = m_documents.At(index);

        if (!item.document)
            return true;

        const std::string fileName = core::fs::ToUtf8(item.path.filename());

        if (auto result = item.document->Save())
        {
            // A document marked Missing whose file this just recreated (Save writes
            // through WriteFileAtomically, which falls through to MoveFileExW onto a
            // path that need not already exist) is still there on disk now - the tab
            // should stop saying "(deleted)". MarkPresent already refuses to touch
            // Binary, TooLarge and ReadFailed, so it is safe to call unconditionally on
            // every successful save rather than only when failure was Missing.
            m_documents.MarkPresent(item.path);
            SetTransientMessage(fileName + " saved");
            return true;
        }
        else
        {
            SetTransientMessage("Could not save " + fileName + ": " + result.GetError().message);
            return false;
        }
    }

    void DocumentWindows::RequestSave(std::size_t index)
    {
        core::text::OpenItem& item = m_documents.At(index);

        if (!item.document)
            return;

        if (item.document->ChangedOnDisk())
        {
            // Never noticed: a dropped notification, with no chance to have asked
            // earlier. This is the last safety net, immediately before the write.
            if (m_overwriteConfirmPath != item.path)
            {
                m_overwriteConfirmPath = item.path;
                m_openOverwriteConfirm = true;
            }

            return;
        }

        SaveOne(index);
    }

    bool DocumentWindows::SaveAllModified()
    {
        std::vector<std::string> conflicted;
        std::size_t savedCount = 0;

        for (std::size_t index = 0; index < m_documents.Count(); ++index)
        {
            const core::text::OpenItem& item = m_documents.At(index);

            if (!item.document || !item.document->IsModified())
                continue;

            // The same safety net a single Ctrl+S gets: ChangedOnDisk() true means a
            // change nobody was told about. But unlike a single Ctrl+S, a batch has no
            // good way to ask about it mid-flight without turning into a sequence of
            // modals - so this document is skipped, not asked about and not silently
            // overwritten, and the batch continues: three other dirty files having
            // nothing wrong with them is no reason to leave them unsaved over a fourth
            // that does. RequestSave (a deliberate per-document Ctrl+S) is still where
            // this document gets its Overwrite/Cancel question.
            if (item.document->ChangedOnDisk())
            {
                conflicted.push_back(core::fs::ToUtf8(item.path.filename()));
                continue;
            }

            // A genuine write failure is different from a conflict: it stops the batch
            // on the spot rather than being collected, since continuing past a real I/O
            // error would be a batch pretending nothing went wrong. SaveOne has already
            // put the specific reason - naming this document - in the status bar, and a
            // summary message here would only bury it.
            if (!SaveOne(index))
                return false;

            ++savedCount;
        }

        if (conflicted.empty())
        {
            if (savedCount > 0)
            {
                SetTransientMessage("Saved " + std::to_string(savedCount)
                                    + (savedCount == 1 ? " file." : " files."));
            }

            return true;
        }

        // Named together in one message, not one message per file overwriting the last:
        // "Saved 4 files. 1 changed on disk and was not saved: Widget.cpp" tells the user
        // exactly what still needs attention instead of leaving them to assume "saved"
        // meant all of them.
        std::string message;

        if (savedCount > 0)
            message = "Saved " + std::to_string(savedCount) + (savedCount == 1 ? " file. " : " files. ");

        message += std::to_string(conflicted.size()) + " changed on disk and "
                  + (conflicted.size() == 1 ? "was" : "were") + " not saved: " + Join(conflicted);

        SetTransientMessage(message);

        return false;
    }

    void DocumentWindows::SetTransientMessage(std::string text)
    {
        m_transientMessage =
            TransientMessage{std::move(text), ImGui::GetTime() + kTransientMessageSeconds};
    }

    std::optional<std::string> DocumentWindows::StatusMessage() const
    {
        if (!m_transientMessage)
            return std::nullopt;

        if (ImGui::GetTime() >= m_transientMessage->expiresAt)
            return std::nullopt;

        return m_transientMessage->text;
    }

    float DocumentWindows::Render(float fontSize, core::text::IndentStyle indentStyle,
                                  unsigned int fallbackDockNode, bool wordWrap)
    {
        // Identities as they stand at the start of this frame - after RequestOpen,
        // CloseAll or HandleRootMoved, all of which run earlier in the same
        // OnUpdate/OnUIRender pass and never inside this function.
        const std::vector<std::string> startingIdentities = CurrentIdentities(m_documents);

        // Taken and cleared in one step: whatever asked for this focus did so earlier in
        // this same pass, so a request that matches no document this frame never will, and
        // leaving it set would hand a stolen focus to whichever document next claims that
        // path.
        const std::optional<std::filesystem::path> focusRequest =
            std::exchange(m_focusRequest, std::nullopt);

        float zoomDelta = 0.0f;

        // Every question below asks the gate before opening, and keeps its request
        // pending when refused - see ModalGate for why they cannot simply all open. Each
        // one also clears its own state when BeginPopupModal comes back false for a popup
        // ImGui does not have open, so a question destroyed by any route this gate did not
        // foresee cannot leave the mechanism behind it permanently wedged.
        if (m_openSizeQuestion && m_modals.TryOpen(kSizeQuestionTitle))
        {
            ImGui::OpenPopup(kSizeQuestionTitle);
            m_openSizeQuestion = false;
        }

        if (ImGui::BeginPopupModal(kSizeQuestionTitle, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            // A size of zero means the probe in RequestOpen failed, which after a
            // successful one moments earlier means the file has gone. Claiming "this file
            // is 0 MB" would be asking permission for a size nobody knows.
            if (m_pendingSize == 0)
            {
                ImGui::TextUnformatted("This file can no longer be read. Load it anyway?");
            }
            else
            {
                ImGui::Text("This file is %llu MB. Load it anyway?",
                            static_cast<unsigned long long>(m_pendingSize / (1024 * 1024)));
            }
            ImGui::Separator();

            if (ImGui::Button("Load"))
            {
                m_documents.OpenConfirmed(m_pendingPath, m_pendingPermanent);
                m_modals.Close(kSizeQuestionTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_modals.Close(kSizeQuestionTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        else if (m_modals.HoldsFor(kSizeQuestionTitle) && !ImGui::IsPopupOpen(kSizeQuestionTitle))
        {
            // The popup this gate believes is on screen is not open: it was dismissed with
            // Escape, or destroyed by something opening over it. Nothing here is a guard
            // for a later mechanism - the file simply does not open - but the gate must
            // not stay held on its behalf.
            m_modals.Close(kSizeQuestionTitle);
        }

        // The reload-conflict, overwrite-confirm and close-confirm modals below all live
        // here, before the per-document loop, for the same reason as the size question
        // above: OpenPopup/BeginPopupModal must not run from inside a Begin/BeginChild
        // pair that may not be current when the popup needs to draw, and this is the one
        // place in Render() that is never nested inside either. The loop below only ever
        // sets the flags and paths these read; the actual ImGui calls happen here.

        // The queue is only popped once the gate is actually free: a question popped into
        // m_reloadConflictActive whose modal never opened would be a question asked of
        // nobody, and m_reloadConflictActive staying set is what would stop every later
        // reload question, for every document, from ever being asked again.
        if (!m_reloadConflictActive && !m_reloadConflictQueue.empty()
            && m_modals.TryOpen(kReloadConflictTitle))
        {
            m_reloadConflictActive = m_reloadConflictQueue.front();
            m_reloadConflictQueue.erase(m_reloadConflictQueue.begin());
            ImGui::OpenPopup(kReloadConflictTitle);
        }

        if (ImGui::BeginPopupModal(kReloadConflictTitle, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string fileName = m_reloadConflictActive
                                             ? core::fs::ToUtf8(m_reloadConflictActive->filename())
                                             : std::string{};

            ImGui::Text("%s changed on disk, and this tab has unsaved changes.",
                        fileName.c_str());
            ImGui::Separator();

            if (ImGui::Button("Reload from disk (lose my changes)"))
            {
                if (m_reloadConflictActive)
                {
                    if (const auto index = m_documents.Find(*m_reloadConflictActive))
                        ReloadClean(*index);
                }

                m_reloadConflictActive.reset();
                m_modals.Close(kReloadConflictTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            // The entire acknowledgement mechanism: recording the file's current state as
            // known is what stops Ctrl+S, or the next sweep, asking about this file again.
            if (ImGui::Button("Keep my version"))
            {
                if (m_reloadConflictActive)
                {
                    if (const auto index = m_documents.Find(*m_reloadConflictActive);
                        index && m_documents.At(*index).document)
                        m_documents.At(*index).document->AcceptDiskState();
                }

                m_reloadConflictActive.reset();
                m_modals.Close(kReloadConflictTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        else if (m_modals.HoldsFor(kReloadConflictTitle) && !ImGui::IsPopupOpen(kReloadConflictTitle))
        {
            // Gone without an answer. m_reloadConflictActive is the guard that lets the
            // queue be popped again, so leaving it set would silence every later
            // "changed on disk" question for the rest of the session. The file itself is
            // not forgotten: ChangedOnDisk() is still true, so the next watcher sweep or
            // focus regain queues it afresh.
            m_reloadConflictActive.reset();
            m_modals.Close(kReloadConflictTitle);
        }

        if (m_openOverwriteConfirm && m_modals.TryOpen(kOverwriteConfirmTitle))
        {
            ImGui::OpenPopup(kOverwriteConfirmTitle);
            m_openOverwriteConfirm = false;
        }

        if (ImGui::BeginPopupModal(kOverwriteConfirmTitle, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string fileName = m_overwriteConfirmPath
                                             ? core::fs::ToUtf8(m_overwriteConfirmPath->filename())
                                             : std::string{};

            ImGui::Text("%s changed on disk since it was opened. Overwrite it?",
                        fileName.c_str());
            ImGui::Separator();

            if (ImGui::Button("Overwrite"))
            {
                if (m_overwriteConfirmPath)
                {
                    if (const auto index = m_documents.Find(*m_overwriteConfirmPath))
                        SaveOne(*index);
                }

                m_overwriteConfirmPath.reset();
                m_modals.Close(kOverwriteConfirmTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_overwriteConfirmPath.reset();
                m_modals.Close(kOverwriteConfirmTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        else if (m_modals.HoldsFor(kOverwriteConfirmTitle)
                 && !ImGui::IsPopupOpen(kOverwriteConfirmTitle))
        {
            // Gone without an answer, and this is the state RequestSave compares against
            // before raising the question again - left set, Ctrl+S on this document would
            // do nothing at all, silently, for the rest of the session.
            m_overwriteConfirmPath.reset();
            m_modals.Close(kOverwriteConfirmTitle);
        }

        // Pop the next close question off the queue, skipping any entry whose document
        // is no longer there to ask about - closed some other way (WorkspaceClosedEvent,
        // say) while it waited its turn. A file merely deleted from disk in the
        // meantime is not skipped: MarkMissing leaves the item and its in-memory content
        // in place, so Save (recreating the file) and Discard both still mean something,
        // and the question stays worth asking.
        //
        // Nothing is popped while the gate is busy - which includes the reload question
        // just above having taken it this very frame. The tab keeps being submitted with
        // its dot until its turn comes; the question is delayed, never lost.
        while (!m_pendingCloseActive && !m_pendingCloseQueue.empty() && !m_modals.IsBusy())
        {
            const std::filesystem::path candidate = m_pendingCloseQueue.front();
            m_pendingCloseQueue.erase(m_pendingCloseQueue.begin());

            if (const auto index = m_documents.Find(candidate);
                index && m_documents.At(*index).document
                && m_documents.At(*index).document->IsModified()
                && m_modals.TryOpen(kCloseConfirmTitle))
            {
                m_pendingCloseActive = candidate;
                ImGui::OpenPopup(kCloseConfirmTitle);
            }
        }

        if (ImGui::BeginPopupModal(kCloseConfirmTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string fileName = m_pendingCloseActive
                                             ? core::fs::ToUtf8(m_pendingCloseActive->filename())
                                             : std::string{};

            ImGui::Text("Save changes to %s before closing?", fileName.c_str());
            ImGui::Separator();

            // Shared by Save and Discard: finds the tab again by path (it may have moved
            // since the question was asked, though never far in practice) and closes it
            // the same way the loop below would have.
            const auto finishClose = [this]
            {
                if (!m_pendingCloseActive)
                    return;

                if (const auto index = m_documents.Find(*m_pendingCloseActive))
                {
                    ImGui::ClearWindowSettings(WindowName(m_documents, *index).c_str());
                    m_documents.Close(*index);
                }

                m_pendingCloseActive.reset();
            };

            if (ImGui::Button("Save"))
            {
                bool saved = true;

                if (m_pendingCloseActive)
                {
                    if (const auto index = m_documents.Find(*m_pendingCloseActive))
                        saved = SaveOne(*index);
                }

                if (saved)
                {
                    finishClose();
                    m_modals.Close(kCloseConfirmTitle);
                    ImGui::CloseCurrentPopup();
                }

                // A failed save changes nothing: the document stays open and dirty, the
                // reason is already in the status bar via SaveOne, and the question stays
                // up so the user can try again or choose Discard/Cancel instead.
            }

            ImGui::SameLine();

            if (ImGui::Button("Discard"))
            {
                finishClose();
                m_modals.Close(kCloseConfirmTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_pendingCloseActive.reset();
                m_modals.Close(kCloseConfirmTitle);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        else if (m_modals.HoldsFor(kCloseConfirmTitle) && !ImGui::IsPopupOpen(kCloseConfirmTitle))
        {
            // Gone without an answer, which counts as Cancel: the tab stays open and
            // dirty. Clearing m_pendingCloseActive matters beyond this one tab - it is
            // what lets the queue behind it move again, and (before this became one
            // question at a time) what a stuck value would have used to refuse every
            // close of the window itself, forever.
            m_pendingCloseActive.reset();
            m_modals.Close(kCloseConfirmTitle);
        }

        for (std::size_t index = 0; index < m_documents.Count();)
        {
            core::text::OpenItem& item = m_documents.At(index);
            const std::string identity = Identity(m_documents, index);
            const std::string name = WindowName(m_documents, index);

            // Whether ImGui has ever heard of this window is the wrong question:
            // ClearWindowSettings (below, and in the purge at the end of this function)
            // undocks a window but does not destroy it, so a document closed and reopened
            // reuses the same ImGuiWindow object FindWindowByName would find - stripped of
            // its dock, with nothing left to redock it. Whether this identity is new
            // *since last frame* is the right question: false for a document that has
            // been open continuously, which is what stops a user's drag from being
            // fought every frame, and true both the first time a document is ever seen
            // and every time it reappears after being closed.
            const bool justOpened = std::find(m_previousIdentities.begin(),
                                              m_previousIdentities.end(), identity)
                                     == m_previousIdentities.end();

            if (justOpened)
            {
                // Checked, not merely non-zero: splitting the editor area, opening a file
                // in the new split and closing it leaves ImGui free to delete the emptied
                // node, and the id kept here outlives it. Docking to a dead id does not
                // fail - DockBuilderDockWindow creates a fresh node under no parent - so
                // the window floats free instead of landing in the centre.
                const bool lastFocusedAlive =
                    m_lastFocusedNode != 0 && ImGui::DockBuilderGetNode(m_lastFocusedNode) != nullptr;

                if (!lastFocusedAlive)
                    m_lastFocusedNode = 0;

                unsigned int targetNode = lastFocusedAlive ? m_lastFocusedNode : fallbackDockNode;

                // A window carrying over a dock position from before its path was
                // rewritten takes priority over both: it is more specific than "wherever
                // was last focused", and right even when nothing has been focused yet.
                if (const auto pending = m_pendingDockTargets.find(identity);
                    pending != m_pendingDockTargets.end())
                {
                    targetNode = pending->second;
                    m_pendingDockTargets.erase(pending);
                }

                if (targetNode != 0)
                    ImGui::DockBuilderDockWindow(name.c_str(), targetNode);
            }

            const bool preview = item.preview;

            // A preview is marked by colour alone. Italic was tried and removed:
            // it could only ever reach this window's own title bar, drawn inside Begin()
            // below when the window is floating, because a docked window's tab is drawn
            // earlier in the frame from inside DockSpace(), and ImGui draws every tab of a
            // node in one pass with one global font - so no font can mark one docked tab
            // and not its neighbours. It would also have collided with the icon now in
            // every title: the MDI glyphs are merged onto the regular mono face alone, so a
            // title drawn in any other face renders its icon as a missing glyph.
            //
            // Colour has neither problem: Begin() -> BeginDocked() calls
            // StoreDockStyleForWindow, which snapshots the style colours in effect at that
            // moment into window->DockStyle, and the tab bar swaps the global style to each
            // window's own snapshot before drawing its tab. So colours pushed here, before
            // Begin, land on this document's tab and no other.
            //
            // The label carries the marker, and the icon in it dims along with the name.
            // That is a cost, not an oversight: a tab's text is one string drawn by one
            // RenderTextEllipsis in one colour, and the icon is a glyph inside that same
            // string, so no per-glyph colour is available without rewriting ImGui's tab
            // rendering.
            //
            // Fading the tab's background instead was tried, and it keeps the icon bright
            // but says the wrong thing: a preview is nearly always the active tab, and an
            // active tab is by construction the brightest item on the bar - lighter fill
            // plus an overline. Fading those only softens a highlight that still reads as
            // "this one is on top" next to a darker inactive neighbour, and fading it far
            // enough to lose that would hide which file is actually open.
            //
            // Position was tried here too, via ImGuiWindowClass::TabItemFlagsOverrideSet,
            // and removed: ImGuiTabItemFlags_Trailing only orders a tab last, it does not
            // pin it to the bar's right edge. TabBarLayout takes ImMin of the right-aligned
            // offset and the packed one, so the trailing section sits straight after the
            // central one whenever the bar has room to spare, and the flag changed nothing
            // visible.
            if (preview)
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            }

            // The dot next to the tab/title, and - for a docked tab - what stops ImGui
            // assuming the X means "close now": the window keeps being torn down from the
            // dock's bookkeeping only if Render stops resubmitting it, which is exactly
            // what the dirty-close branch below refuses to do until the question resolves.
            const bool modified = item.document && item.document->IsModified();
            const ImGuiWindowFlags windowFlags =
                modified ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;

            // Picked in the tree while already open. Begin() is what turns this into a
            // selected tab on a docked window, which is why it is set here rather than
            // ImGui::SetWindowFocus being called from RequestOpen against a window that
            // may not have been submitted yet this frame.
            if (focusRequest == item.path)
                ImGui::SetNextWindowFocus();

            bool open = true;
            const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

            // Popped the moment Begin returns, so the dimming reaches this document's tab
            // and title bar without following it into the editor's own contents.
            if (preview)
                ImGui::PopStyleColor();

            if (visible)
            {
                if (ImGui::IsWindowFocused())
                {
                    m_documents.Activate(index);

                    if (ImGuiWindow* window = ImGui::GetCurrentWindow();
                        window != nullptr && window->DockNode != nullptr)
                        m_lastFocusedNode = window->DockNode->ID;
                }

                // A double click on the tab promotes a preview, the same as one in the
                // tree does.
                if (preview && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    m_documents.Promote(index);

                const EditorFrameResult editorResult =
                    DrawEditor(item, fontSize, indentStyle, wordWrap);
                zoomDelta += editorResult.zoomDelta;

                // The first edit promotes a preview tab exactly as a double click does:
                // once the user has typed into it, it is no longer something they were
                // just glancing at.
                if (editorResult.edited && preview)
                    m_documents.Promote(index);

                if (editorResult.saveRequested)
                    RequestSave(index);
            }

            ImGui::End();

            if (!open)
            {
                if (modified)
                {
                    // Both versions are somebody's work: not closed here. The tab keeps
                    // being submitted every frame (falling through to ++index below)
                    // until its turn comes up in the queue above and Save, Discard or
                    // Cancel resolves it. Queued rather than replacing whatever is
                    // already pending, so a second tab's close button pressed before the
                    // first question resolves still gets its own question, in order,
                    // instead of being silently dropped.
                    if (m_pendingCloseActive != item.path
                        && std::find(m_pendingCloseQueue.begin(), m_pendingCloseQueue.end(),
                                    item.path)
                               == m_pendingCloseQueue.end())
                    {
                        m_pendingCloseQueue.push_back(item.path);
                    }
                }
                else
                {
                    // The layout entry goes with the window: position belongs to the
                    // group, not to the file, so a reopened document lands where the user
                    // is working rather than where it was months ago - and imgui.ini
                    // stops collecting a dead section per file ever opened.
                    ImGui::ClearWindowSettings(name.c_str());
                    m_documents.Close(index);
                    continue;
                }
            }

            ++index;
        }

        // Ctrl+Shift+S over every dirty document. RouteGlobal, the same as
        // GraphiteLayer's zoom shortcuts and for the same reason: this runs after every
        // window this frame has already Begin/End'd, so there is no "current" window a
        // RouteFocused check could compare against.
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                            ImGuiInputFlags_RouteGlobal))
        {
            SaveAllModified();
        }

        // Purges whatever left the document set since the last time this ran without
        // going through this loop's own close button above - see m_previousIdentities.
        // Deliberately runs after the loop: a renamed document's pending dock target has
        // to be consumed by Begin() - inside the loop above - before its old identity's
        // dock node can safely be undocked and possibly deleted here. Purging first would
        // let ClearWindowSettings tear down a shared node before DockBuilderDockWindow's
        // deferred settings entry for the new identity ever gets to bind to it, stranding
        // the renamed document in a fresh orphan node instead of its old spot.
        for (const std::string& previousIdentity : m_previousIdentities)
        {
            if (std::find(startingIdentities.begin(), startingIdentities.end(), previousIdentity)
                == startingIdentities.end())
                ImGui::ClearWindowSettings(previousIdentity.c_str());
        }

        m_previousIdentities = CurrentIdentities(m_documents);

        return zoomDelta;
    }
}
