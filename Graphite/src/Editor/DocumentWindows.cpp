#include "Editor/DocumentWindows.h"

#include "Editor/TextEditorWidget.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include <imgui.h>
#include <imgui_internal.h>

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

    void DocumentWindows::Render(float fontSize)
    {
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

            if (m_lastFocusedNode != 0 && ImGui::FindWindowByName(name.c_str()) == nullptr)
                ImGui::DockBuilderDockWindow(name.c_str(), m_lastFocusedNode);

            const bool preview = item.preview;

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
    }
}
