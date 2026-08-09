#include "Editor/DocumentWindows.h"

#include "Editor/TextEditorWidget.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <system_error>

namespace graphite
{
    namespace
    {
        constexpr const char* kSizeQuestionTitle = "Large file##EditorSizeQuestion";

        [[nodiscard]] std::string WindowName(const core::text::DocumentSet& documents,
                                             std::size_t index)
        {
            // ImGui shows what precedes ### and identifies by what follows, so the path -
            // the only identity that cannot collide - stays out of sight.
            return documents.DisplayTitle(index) + "###"
                   + core::fs::ToUtf8(documents.At(index).path);
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

        if (outcome != core::text::OpenOutcome::NeedsConfirmation)
            return;

        m_pendingPath = path;
        m_pendingPermanent = permanent;
        m_openSizeQuestion = true;
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

            if (item.failure == core::text::OpenFailure::Missing)
                continue;

            // The watcher names directories, so an item is worth a stat only when its own
            // directory is one of them. `everything` means the batch could not be narrowed
            // down at all - dropped notifications, or a git state change - and every open
            // document has to be checked.
            if (!everything
                && std::find(directories.begin(), directories.end(), item.path.parent_path())
                       == directories.end())
                continue;

            std::error_code error;
            const bool present = std::filesystem::exists(item.path, error);

            // An error is not an answer. A share that blinked or a directory that briefly
            // denied access must not turn every document open on it into a deleted one -
            // the same reasoning SyncRepository applies before dropping a repository.
            if (!error && !present)
                m_documents.MarkMissing(item.path);
        }
    }

    float DocumentWindows::Render(float fontSize, unsigned int fallbackDockNode)
    {
        // Identities as they stand at the start of this frame - after RequestOpen,
        // CloseAll or HandleRootMoved, all of which run earlier in the same
        // OnUpdate/OnUIRender pass and never inside this function.
        const std::vector<std::string> startingIdentities = CurrentIdentities(m_documents);

        float zoomDelta = 0.0f;

        if (m_openSizeQuestion)
        {
            ImGui::OpenPopup(kSizeQuestionTitle);
            m_openSizeQuestion = false;
        }

        if (ImGui::BeginPopupModal(kSizeQuestionTitle, nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto probe = core::text::ProbeFile(m_pendingPath);
            const std::uint64_t megabytes =
                probe ? probe.Value().size / (1024 * 1024) : 0;

            ImGui::Text("This file is %llu MB. Load it anyway?",
                        static_cast<unsigned long long>(megabytes));
            ImGui::Separator();

            if (ImGui::Button("Load"))
            {
                m_documents.OpenConfirmed(m_pendingPath, m_pendingPermanent);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
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
                unsigned int targetNode = m_lastFocusedNode != 0 ? m_lastFocusedNode : fallbackDockNode;

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

            // Only ever reaches this window's own title bar, drawn inside Begin() below
            // when the window is floating. A docked preview's tab is drawn earlier in the
            // frame, before this runs - see the comment on MonoItalicFont() - so a docked
            // preview looks the same as a docked permanent tab. Nothing in this stage
            // gives the preview state a second, tab-visible marker.
            if (preview)
                ImGui::PushFont(core::ImGuiRenderer::MonoItalicFont(), 0.0f);

            bool open = true;
            const bool visible = ImGui::Begin(name.c_str(), &open);

            if (preview)
                ImGui::PopFont();

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

                zoomDelta += DrawEditor(item, fontSize);
            }

            ImGui::End();

            if (!open)
            {
                // The layout entry goes with the window: position belongs to the group,
                // not to the file, so a reopened document lands where the user is working
                // rather than where it was months ago - and imgui.ini stops collecting a
                // dead section per file ever opened.
                ImGui::ClearWindowSettings(name.c_str());
                m_documents.Close(index);
                continue;
            }

            ++index;
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
