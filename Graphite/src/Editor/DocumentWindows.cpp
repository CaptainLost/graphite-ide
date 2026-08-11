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

        LargeFileQuestion question{path, permanent, 0};

        // Probed once, here, rather than every frame the modal is open: a stat plus an
        // 8 KB read per frame is real work when the file sits on a network share, and the
        // answer cannot change between the question and the reply in any way worth
        // redrawing for. Open() has just probed the same file successfully - that is what
        // NeedsConfirmation means - so this second probe is asking a question already
        // answered, and a failure here means the file went away in between.
        if (const auto probe = core::text::ProbeFile(path))
        {
            question.size = probe.Value().size;
        }

        m_sizeQuestion.Ask(std::move(question));
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

        // Dirty: ask rather than reload, and let the question queue, so several files
        // changing in the same batch (a git checkout) are asked about one at a time
        // instead of every modal fighting to open this frame. Two sweeps naming the same
        // file - a watcher batch and a focus regain moments later - are one question, not
        // the same one asked twice over.
        if (!m_reloadConflict.Asked(item.path))
        {
            m_reloadConflict.Ask(item.path);
        }
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
            if (!m_overwriteConfirm.Asked(item.path))
            {
                m_overwriteConfirm.Ask(item.path);
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
                                  unsigned int fallbackDockNode)
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

        // Every question below asks the gate before opening and keeps whatever it was
        // asked about pending when refused - see ModalGate for why they cannot simply all
        // open. Neither that nor the check for a popup ImGui no longer has open is written
        // out here: ModalQuestion::Begin does both for every question there is, so no
        // question drawn from this function can leave the mechanism behind it wedged.
        if (m_sizeQuestion.Begin())
        {
            const LargeFileQuestion* question = m_sizeQuestion.Current();
            const std::uint64_t size = question != nullptr ? question->size : 0;

            // A size of zero means the probe in RequestOpen failed, which after a
            // successful one moments earlier means the file has gone. Claiming "this file
            // is 0 MB" would be asking permission for a size nobody knows.
            if (size == 0)
            {
                ImGui::TextUnformatted("This file can no longer be read. Load it anyway?");
            }
            else
            {
                ImGui::Text("This file is %llu MB. Load it anyway?",
                            static_cast<unsigned long long>(size / (1024 * 1024)));
            }
            ImGui::Separator();

            if (ImGui::Button("Load"))
            {
                // Before Answer(), which drops the payload this still points into.
                if (question != nullptr)
                {
                    m_documents.OpenConfirmed(question->path, question->permanent);
                }

                m_sizeQuestion.Answer();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_sizeQuestion.Answer();
            }

            m_sizeQuestion.End();
        }

        // The reload-conflict, overwrite-confirm and close-confirm modals below all live
        // here, before the per-document loop, for the same reason as the size question
        // above: OpenPopup/BeginPopupModal must not run from inside a Begin/BeginChild
        // pair that may not be current when the popup needs to draw, and this is the one
        // place in Render() that is never nested inside either. The loop below only ever
        // asks the questions drawn here; the actual ImGui calls happen here.

        if (m_reloadConflict.Begin())
        {
            const std::filesystem::path* path = m_reloadConflict.Current();
            const std::string fileName =
                path != nullptr ? core::fs::ToUtf8(path->filename()) : std::string{};

            ImGui::Text("%s changed on disk, and this tab has unsaved changes.",
                        fileName.c_str());
            ImGui::Separator();

            if (ImGui::Button("Reload from disk (lose my changes)"))
            {
                if (path != nullptr)
                {
                    if (const auto index = m_documents.Find(*path))
                    {
                        ReloadClean(*index);
                    }
                }

                m_reloadConflict.Answer();
            }

            ImGui::SameLine();

            // The entire acknowledgement mechanism: recording the file's current state as
            // known is what stops Ctrl+S, or the next sweep, asking about this file again.
            if (ImGui::Button("Keep my version"))
            {
                if (path != nullptr)
                {
                    if (const auto index = m_documents.Find(*path);
                        index && m_documents.At(*index).document)
                    {
                        m_documents.At(*index).document->AcceptDiskState();
                    }
                }

                m_reloadConflict.Answer();
            }

            m_reloadConflict.End();
        }

        if (m_overwriteConfirm.Begin())
        {
            const std::filesystem::path* path = m_overwriteConfirm.Current();
            const std::string fileName =
                path != nullptr ? core::fs::ToUtf8(path->filename()) : std::string{};

            ImGui::Text("%s changed on disk since it was opened. Overwrite it?",
                        fileName.c_str());
            ImGui::Separator();

            if (ImGui::Button("Overwrite"))
            {
                if (path != nullptr)
                {
                    if (const auto index = m_documents.Find(*path))
                    {
                        SaveOne(*index);
                    }
                }

                m_overwriteConfirm.Answer();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_overwriteConfirm.Answer();
            }

            m_overwriteConfirm.End();
        }

        // Asked only of a document still there to ask about, and still dirty: one closed
        // some other way (WorkspaceClosedEvent, say) while its question waited its turn
        // has nothing left to save, and neither has one saved in the meantime. Checked
        // when the question comes off the queue rather than when it joined, since all of
        // that can happen while it waits. A file merely deleted from disk in the meantime
        // still gets its question: MarkMissing leaves the item and its in-memory content
        // in place, so Save (recreating the file) and Discard both still mean something.
        const auto stillModified = [this](const std::filesystem::path& candidate)
        {
            const auto index = m_documents.Find(candidate);

            return index && m_documents.At(*index).document
                   && m_documents.At(*index).document->IsModified();
        };

        if (m_closeConfirm.Begin(stillModified))
        {
            const std::filesystem::path* path = m_closeConfirm.Current();
            const std::string fileName =
                path != nullptr ? core::fs::ToUtf8(path->filename()) : std::string{};

            ImGui::Text("Save changes to %s before closing?", fileName.c_str());
            ImGui::Separator();

            // Shared by Save and Discard: finds the tab again by path (it may have moved
            // since the question was asked, though never far in practice) and closes it
            // the same way the loop below would have. Called before Answer(), which drops
            // the payload `path` points into.
            const auto finishClose = [this, path]
            {
                if (path == nullptr)
                {
                    return;
                }

                if (const auto index = m_documents.Find(*path))
                {
                    ImGui::ClearWindowSettings(WindowName(m_documents, *index).c_str());
                    m_documents.Close(*index);
                }
            };

            if (ImGui::Button("Save"))
            {
                bool saved = true;

                if (path != nullptr)
                {
                    if (const auto index = m_documents.Find(*path))
                    {
                        saved = SaveOne(*index);
                    }
                }

                if (saved)
                {
                    finishClose();
                    m_closeConfirm.Answer();
                }

                // A failed save changes nothing: the document stays open and dirty, the
                // reason is already in the status bar via SaveOne, and the question stays
                // up so the user can try again or choose Discard/Cancel instead.
            }

            ImGui::SameLine();

            if (ImGui::Button("Discard"))
            {
                finishClose();
                m_closeConfirm.Answer();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                m_closeConfirm.Answer();
            }

            m_closeConfirm.End();
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

                const EditorFrameResult editorResult = DrawEditor(item, fontSize, indentStyle);
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
                    // instead of being silently dropped - and the tab's close button
                    // pressed again while its own question waits does not queue it twice.
                    if (!m_closeConfirm.Asked(item.path))
                    {
                        m_closeConfirm.Ask(item.path);
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
