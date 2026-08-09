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

        [[nodiscard]] std::vector<std::string> CurrentNames(const core::text::DocumentSet& documents)
        {
            std::vector<std::string> names;
            names.reserve(documents.Count());

            for (std::size_t index = 0; index < documents.Count(); ++index)
                names.push_back(WindowName(documents, index));

            return names;
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
        const std::vector<std::string> oldNames = CurrentNames(m_documents);

        m_documents.RewritePaths(from, to);

        const std::vector<std::string> newNames = CurrentNames(m_documents);

        // RewritePaths only ever changes paths in place, never the count or the order, so
        // the two snapshots line up index for index.
        for (std::size_t index = 0; index < newNames.size(); ++index)
        {
            if (newNames[index] == oldNames[index])
                continue;

            // The old window only exists to ask about if it was ever drawn this session.
            // When it was, and it had settled into a dock node, carry that node over to
            // the new name so the document does not visibly jump to the fallback node the
            // next time it is drawn. imgui.ini's entry under the old name is left for
            // Render's own bookkeeping to purge, the same way any other document that
            // stops existing between frames is purged.
            if (ImGuiWindow* oldWindow = ImGui::FindWindowByName(oldNames[index].c_str());
                oldWindow != nullptr && oldWindow->DockNode != nullptr)
            {
                m_pendingDockTargets[newNames[index]] = oldWindow->DockNode->ID;
            }
        }
    }

    void DocumentWindows::Render(float fontSize, unsigned int fallbackDockNode)
    {
        // Catches whatever left the document set since the last time this ran without
        // going through this loop's own close button - see m_previousNames.
        {
            const std::vector<std::string> currentNames = CurrentNames(m_documents);

            for (const std::string& previousName : m_previousNames)
            {
                if (std::find(currentNames.begin(), currentNames.end(), previousName)
                    == currentNames.end())
                    ImGui::ClearWindowSettings(previousName.c_str());
            }
        }

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
            const std::string name = WindowName(m_documents, index);

            if (ImGui::FindWindowByName(name.c_str()) == nullptr)
            {
                unsigned int targetNode = m_lastFocusedNode != 0 ? m_lastFocusedNode : fallbackDockNode;

                // A window carrying over a dock position from before its path was
                // rewritten takes priority over both: it is more specific than "wherever
                // was last focused", and right even when nothing has been focused yet.
                if (const auto pending = m_pendingDockTargets.find(name);
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

                DrawEditor(item, fontSize);
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

        m_previousNames = CurrentNames(m_documents);
    }
}
