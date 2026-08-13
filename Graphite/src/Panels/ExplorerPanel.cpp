#include "Panels/ExplorerPanel.h"

#include "AppSettings.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace graphite
{
    namespace
    {
        constexpr const char* kModeKey = "explorerMode";

        // Adjectives, not verbs: the tick beside one of them makes these a state rather than
        // a command, and four levels deep "Dim" alone no longer says what it would dim. The
        // submenu names the subject and these name the treatment, so it reads as
        // "Ignored Files: Dimmed".
        struct DisplayMode
        {
            const char* id;
            const char* label;
            GitIgnoredDisplay display;
        };

        constexpr DisplayMode kDisplayModes[] = {
            {"git.ignored.show", "Visible", GitIgnoredDisplay::Show},
            {"git.ignored.dim", "Dimmed", GitIgnoredDisplay::Dim},
            {"git.ignored.hide", "Hidden", GitIgnoredDisplay::Hide},
        };
    }

    ExplorerPanel::ExplorerPanel(Workspace& workspace, const core::text::DocumentSet& documents)
        : core::Panel("files", "Explorer"), m_workspace(workspace),
          m_files(workspace, documents), m_solution(workspace)
    {
    }

    void ExplorerPanel::OnUIRender()
    {
        SyncModeWithWorkspace();
        RenderHeader();
        ImGui::Separator();

        if (m_mode == Mode::Files)
            m_files.Render();
        else
            m_solution.Render();
    }

    void ExplorerPanel::SyncModeWithWorkspace()
    {
        if (m_workspace.Root() == m_modeLoadedFor)
            return;

        m_modeLoadedFor = m_workspace.Root();

        // Anything unrecognised — including the empty string of a folder opened for the
        // first time — reads as Files, which is the mode that works in every folder.
        m_mode = m_workspace.FolderSettings().Get(kModeKey) == "solution" ? Mode::Solution
                                                                         : Mode::Files;
    }

    void ExplorerPanel::SetMode(Mode mode)
    {
        m_mode = mode;

        // Stored per folder, which is what keeps a folder with no solution from opening
        // into an empty Solution view: it never recorded that mode to begin with.
        m_workspace.FolderSettings().Set(kModeKey, mode == Mode::Solution ? "solution" : "files");
    }

    void ExplorerPanel::RenderHeader()
    {
        // Named in full rather than just "Solution": the word alone would not say
        // which kind, and premake workspaces or CMake projects would each want the
        // same slot with different meaning.
        // "File System" rather than "Files" or "Folder": it says what you are looking at,
        // and "folder" already means three different things in this panel — a directory on
        // disk, a solution folder, and a filter folder inside a project.
        const char* current = m_mode == Mode::Files ? "File System" : "Visual Studio Solution";

        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::BeginCombo("##explorerMode", current))
        {
            if (ImGui::Selectable("File System", m_mode == Mode::Files))
                SetMode(Mode::Files);

            if (ImGui::Selectable("Visual Studio Solution", m_mode == Mode::Solution))
                SetMode(Mode::Solution);

            ImGui::EndCombo();
        }

        // Only worth the row when there is a choice to make. One solution needs no
        // chooser, and none needs it even less.
        if (m_mode != Mode::Solution || m_workspace.SolutionCandidates().size() < 2)
            return;

        // A value rather than a reference: the false branch of the conditional expression
        // below is a temporary, and binding a const reference to it here would leave
        // `loaded` dangling for the rest of the function.
        const std::filesystem::path loaded =
            m_workspace.LoadedSolution() ? m_workspace.LoadedSolution()->path : std::filesystem::path{};

        ImGui::SetNextItemWidth(-1.0f);

        if (!ImGui::BeginCombo("##solutionChoice", core::fs::ToUtf8(loaded.filename()).c_str()))
            return;

        for (const std::filesystem::path& candidate : m_workspace.SolutionCandidates())
        {
            const std::string name = core::fs::ToUtf8(candidate.filename());

            if (ImGui::Selectable(name.c_str(), candidate == loaded))
                m_workspace.SelectSolution(candidate);
        }

        ImGui::EndCombo();
    }

    void ExplorerPanel::LoadSettings()
    {
        Apply(GitIgnoredDisplayFromString(AppSettings::LoadGitIgnoredDisplay()), false);
    }

    void ExplorerPanel::RegisterCommands(core::CommandRegistry& commands)
    {
        for (const DisplayMode& mode : kDisplayModes)
        {
            commands.Add({.id = mode.id,
                          .label = mode.label,
                          .run = [this, mode] { Apply(mode.display, true); },
                          .checked = [this, mode] { return m_display == mode.display; }});
        }
    }

    void ExplorerPanel::Apply(GitIgnoredDisplay display, bool persist)
    {
        m_display = display;
        m_files.SetGitIgnoredDisplay(display);

        if (persist)
        {
            AppSettings::SaveGitIgnoredDisplay(GitIgnoredDisplayToString(display));
        }
    }
}
