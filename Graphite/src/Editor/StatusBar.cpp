#include "Editor/StatusBar.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace graphite
{
    namespace
    {
        [[nodiscard]] const char* LineEndingName(core::text::LineEnding ending, bool mixed)
        {
            if (ending == core::text::LineEnding::Crlf)
                return mixed ? "CRLF (mixed)" : "CRLF";

            return mixed ? "LF (mixed)" : "LF";
        }

        [[nodiscard]] std::string RelativePath(const std::filesystem::path& path,
                                               const std::filesystem::path& root)
        {
            if (root.empty())
                return core::fs::ToUtf8(path);

            const auto relative = path.lexically_relative(root);

            if (relative.empty() || *relative.begin() == "..")
                return core::fs::ToUtf8(path);

            return core::fs::ToUtf8(relative);
        }
    }

    void DrawStatusBar(const core::text::DocumentSet& documents,
                       const std::filesystem::path& root)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float height = ImGui::GetFrameHeight();

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

        if (!ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags))
        {
            ImGui::End();
            return;
        }

        const auto active = documents.Active();

        if (active && documents.At(*active).document)
        {
            const core::text::OpenItem& item = documents.At(*active);
            const core::text::TextBuffer& buffer = item.document->Buffer();

            // The strip is exactly one frame tall: height == FontSize + 2 * FramePadding.y
            // (that is what GetFrameHeight() above returns), and a line of text is
            // FontSize tall (GetTextLineHeight()). Centering it means leaving
            // (height - textHeight) / 2 == FramePadding.y of empty space above the text,
            // which is what SetCursorPosY places the cursor at — measured from the
            // window's own top edge, the same origin GetCursorPosY() uses, so this holds
            // regardless of WindowPadding and stays centred if the font size changes at
            // runtime. Both sides of the strip share one cursor row, so SameLine() below
            // carries this same Y to the right-hand group.
            const float textHeight = ImGui::GetTextLineHeight();
            ImGui::SetCursorPosY((height - textHeight) * 0.5f);

            ImGui::TextUnformatted(RelativePath(item.path, root).c_str());

            const core::text::Position where = buffer.ToPosition(item.caret.offset);
            const std::size_t column = core::text::ColumnOfByte(
                buffer.LineText(where.line), where.column, core::text::DefaultTabWidth);

            // One-based, because that is how people and every compiler count.
            std::string position = "Ln " + std::to_string(where.line + 1) + ", Col "
                                   + std::to_string(column + 1);

            const auto [from, to] = core::text::SelectionRange(item.caret);

            if (from != to)
                position += " (" + std::to_string(to - from) + " selected)";

            const std::string encoding{core::text::EncodingName(item.document->FileEncoding())};
            const std::string ending =
                LineEndingName(item.document->DocumentLineEnding(), item.document->WasMixed());

            const std::string right = position + "    " + encoding + "    " + ending;
            const float width = ImGui::CalcTextSize(right.c_str()).x;

            ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x);
            ImGui::TextUnformatted(right.c_str());
        }

        ImGui::End();
    }
}
