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
		constexpr const char* kFileSystemLabel = "File System";
		constexpr const char* kSolutionLabel = "Visual Studio Solution";

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
		m_Mode = ModeFromSetting(m_Workspace.FolderSettings().Read(kModeKey));
	}

	ExplorerPanel::Mode ExplorerPanel::ModeFromSetting(const std::string& value)
	{
		return value == "solution" ? Mode::Solution : Mode::Files;
	}

	const char* ExplorerPanel::SettingForMode(Mode mode)
	{
		return mode == Mode::Solution ? "solution" : "files";
	}

	void ExplorerPanel::SetMode(Mode mode)
	{
		m_Mode = mode;
		m_Workspace.FolderSettings().Write(kModeKey, SettingForMode(mode));
	}

	void ExplorerPanel::RenderHeader()
	{
		RenderModeCombo();

		if (m_Mode == Mode::Solution && m_Workspace.GetSolutionCandidates().size() > 1)
			RenderSolutionChooser();
	}

	void ExplorerPanel::RenderModeCombo()
	{
		const char* current = m_Mode == Mode::Files ? kFileSystemLabel : kSolutionLabel;

		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::BeginCombo("##explorerMode", current))
		{
			if (ImGui::Selectable(kFileSystemLabel, m_Mode == Mode::Files))
				SetMode(Mode::Files);

			if (ImGui::Selectable(kSolutionLabel, m_Mode == Mode::Solution))
				SetMode(Mode::Solution);

			ImGui::EndCombo();
		}
	}

	void ExplorerPanel::RenderSolutionChooser()
	{
		// A value, not a reference: the false branch below is a temporary that a const
		// reference would be left dangling on.
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