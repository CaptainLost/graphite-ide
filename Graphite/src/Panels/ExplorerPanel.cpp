#include "Panels/ExplorerPanel.h"

#include "Settings/AppSettings.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Fs/FileSystem.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace Graphite {

	namespace {
		constexpr const char* kModeKey = "explorerMode";

		// Adjectives, not verbs: the tick beside one of them makes these a state rather than
		// a command, and four levels deep "Dim" alone no longer says what it would dim. The
		// submenu names the subject and these name the treatment, so it reads as
		// "Ignored Files: Dimmed".
		struct DisplayMode
		{
			const char* Id;
			const char* Label;
			GitIgnoredDisplay Display;
		};

		constexpr DisplayMode kDisplayModes[] = {
			{"git.ignored.show", "Visible", GitIgnoredDisplay::Show},
			{"git.ignored.dim", "Dimmed", GitIgnoredDisplay::Dim},
			{"git.ignored.hide", "Hidden", GitIgnoredDisplay::Hide},
		};
	}

	ExplorerPanel::ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents)
		: Core::Panel("files", "Explorer"), m_Workspace(workspace),
		  m_Files(workspace, documents), m_Solution(workspace)
	{
	}

	void ExplorerPanel::OnImGuiRender()
	{
		SyncModeWithWorkspace();
		RenderHeader();
		ImGui::Separator();

		if (m_Mode == Mode::Files)
			m_Files.Render();
		else
			m_Solution.Render();
	}

	void ExplorerPanel::SyncModeWithWorkspace()
	{
		if (m_Workspace.GetRoot() == m_ModeLoadedFor)
			return;

		m_ModeLoadedFor = m_Workspace.GetRoot();

		// Anything unrecognised — including the empty string of a folder opened for the
		// first time — reads as Files, which is the mode that works in every folder.
		m_Mode = m_Workspace.FolderSettings().Get(kModeKey) == "solution" ? Mode::Solution
																		  : Mode::Files;
	}

	void ExplorerPanel::SetMode(Mode mode)
	{
		m_Mode = mode;

		// Stored per folder, which is what keeps a folder with no solution from opening
		// into an empty Solution view: it never recorded that mode to begin with.
		m_Workspace.FolderSettings().Set(kModeKey, mode == Mode::Solution ? "solution" : "files");
	}

	void ExplorerPanel::RenderHeader()
	{
		// Named in full rather than just "Solution": the word alone would not say
		// which kind, and premake workspaces or CMake projects would each want the
		// same slot with different meaning.
		// "File System" rather than "Files" or "Folder": it says what you are looking at,
		// and "folder" already means three different things in this panel — a directory on
		// disk, a solution folder, and a filter folder inside a project.
		const char* current = m_Mode == Mode::Files ? "File System" : "Visual Studio Solution";

		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::BeginCombo("##explorerMode", current))
		{
			if (ImGui::Selectable("File System", m_Mode == Mode::Files))
				SetMode(Mode::Files);

			if (ImGui::Selectable("Visual Studio Solution", m_Mode == Mode::Solution))
				SetMode(Mode::Solution);

			ImGui::EndCombo();
		}

		// Only worth the row when there is a choice to make. One solution needs no
		// chooser, and none needs it even less.
		if (m_Mode != Mode::Solution || m_Workspace.GetSolutionCandidates().size() < 2)
			return;

		// A value rather than a reference: the false branch of the conditional expression
		// below is a temporary, and binding a const reference to it here would leave
		// `loaded` dangling for the rest of the function.
		const std::filesystem::path loaded =
			m_Workspace.LoadedSolution() ? m_Workspace.LoadedSolution()->Path : std::filesystem::path{};

		ImGui::SetNextItemWidth(-1.0f);

		if (!ImGui::BeginCombo("##solutionChoice", Core::ToUtf8(loaded.filename()).c_str()))
			return;

		for (const std::filesystem::path& candidate : m_Workspace.GetSolutionCandidates())
		{
			const std::string name = Core::ToUtf8(candidate.filename());

			if (ImGui::Selectable(name.c_str(), candidate == loaded))
				m_Workspace.SelectSolution(candidate);
		}

		ImGui::EndCombo();
	}

	void ExplorerPanel::LoadSettings()
	{
		Apply(GitIgnoredDisplayFromString(AppSettings::LoadGitIgnoredDisplay()), false);
	}

	void ExplorerPanel::RegisterCommands(Core::CommandRegistry& commands)
	{
		for (const DisplayMode& mode : kDisplayModes)
		{
			commands.Add({.Id = mode.Id,
						  .Label = mode.Label,
						  .Run = [this, mode] { Apply(mode.Display, true); },
						  .Checked = [this, mode] { return m_Display == mode.Display; }});
		}
	}

	void ExplorerPanel::Apply(GitIgnoredDisplay display, bool persist)
	{
		m_Display = display;
		m_Files.SetGitIgnoredDisplay(display);

		if (persist)
		{
			AppSettings::SaveGitIgnoredDisplay(GitIgnoredDisplayToString(display));
		}
	}
}