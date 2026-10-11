#include "Explorer/ExplorerPanel.h"

#include "Settings/AppSettings.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <filesystem>
#include <string>
#include <utility>

namespace Graphite {

	namespace {
		constexpr const char* kModeKey = "explorerMode";
		constexpr const char* kFileSystemLabel = "File System";
		constexpr const char* kSolutionLabel = "Visual Studio Solution";

		struct DisplayMode
		{
			const char* Id;
			const char* Label;
			const char* ListLabel;
			GitIgnoredDisplay Display;
		};

		// Which files these three are about is said by the submenu they hang under, so the
		// labels only have to say what happens to them. The settings screen, which has a
		// heading of its own, passes its own labels. The list of key bindings has neither, so
		// it reads the longer ones.
		constexpr DisplayMode kDisplayModes[] = {
			{"git.ignored.show", "Show", "Ignored Files: Show", GitIgnoredDisplay::Show},
			{"git.ignored.dim", "Dim", "Ignored Files: Dim", GitIgnoredDisplay::Dim},
			{"git.ignored.hide", "Hide", "Ignored Files: Hide", GitIgnoredDisplay::Hide},
		};
	}

	ExplorerPanel::ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents,
								 AppSettings& settings, Core::EventBus& events, Core::JobSubmitter submitJob)
		: Core::Panel("files", "Explorer", "navigate", ICON_MDI_FILE_TREE), m_Workspace(workspace),
		  m_AppSettings(settings), m_Files(workspace, documents, events, std::move(submitJob)),
		  m_Solution(workspace, documents, events)
	{
		// The folder's stored mode is read when the panel first sees a folder, and would put a
		// just-opened solution back behind the file tree. Handled here, after the workspace has
		// finished switching, and the folder is marked as seen so that read does not follow.
		events.Subscribe<SolutionOpenedEvent>([this](const SolutionOpenedEvent&) {
			m_ModeLoadedFor = m_Workspace.GetRoot();
			SetMode(Mode::Solution);
		});
	}

	void ExplorerPanel::OnImGuiRender()
	{
		SyncModeWithWorkspace();
		RenderHeader();
		ImGui::Separator();

		if (m_Mode == Mode::Files)
		{
			m_Files.Render();
		}
		else
		{
			m_Solution.Render();
		}
	}

	void ExplorerPanel::SetGitDecorations(const GitDecorationView& decorations)
	{
		m_Files.SetGitDecorations(decorations);
		m_Solution.SetGitDecorations(decorations);
	}

	void ExplorerPanel::SyncModeWithWorkspace()
	{
		if (m_Workspace.GetRoot() == m_ModeLoadedFor)
		{
			return;
		}

		m_ModeLoadedFor = m_Workspace.GetRoot();
		m_Mode = ModeFromSetting(m_Workspace.GetFolderSettings().Read(kModeKey));
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
		m_Workspace.GetFolderSettings().Write(kModeKey, SettingForMode(mode));
	}

	void ExplorerPanel::RenderHeader()
	{
		RenderModeCombo();

		if (m_Mode == Mode::Solution && m_Workspace.GetSolutionCandidates().size() > 1)
		{
			RenderSolutionChooser();
		}
	}

	void ExplorerPanel::RenderModeCombo()
	{
		const char* current = m_Mode == Mode::Files ? kFileSystemLabel : kSolutionLabel;

		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::BeginCombo("##explorerMode", current))
		{
			if (ImGui::Selectable(kFileSystemLabel, m_Mode == Mode::Files))
			{
				SetMode(Mode::Files);
			}

			if (ImGui::Selectable(kSolutionLabel, m_Mode == Mode::Solution))
			{
				SetMode(Mode::Solution);
			}

			ImGui::EndCombo();
		}
	}

	void ExplorerPanel::RenderSolutionChooser()
	{
		// A value, not a reference: the false branch below is a temporary that a const
		// reference would be left dangling on.
		const std::filesystem::path loaded =
			m_Workspace.GetLoadedSolution() ? m_Workspace.GetLoadedSolution()->Path : std::filesystem::path{};

		ImGui::SetNextItemWidth(-1.0f);

		if (!ImGui::BeginCombo("##solutionChoice", Core::ToUtf8(loaded.filename()).c_str()))
		{
			return;
		}

		for (const std::filesystem::path& candidate : m_Workspace.GetSolutionCandidates())
		{
			const std::string name = Core::ToUtf8(candidate.filename());

			if (ImGui::Selectable(name.c_str(), candidate == loaded))
			{
				m_Workspace.SelectSolution(candidate);
			}
		}

		ImGui::EndCombo();
	}

	void ExplorerPanel::LoadSettings()
	{
		Apply(GitIgnoredDisplayFromString(m_AppSettings.GetGitIgnoredDisplay()), false);
		ApplyFollowsActiveFile(m_AppSettings.IsExplorerFollowingActiveFile(), false);
	}

	void ExplorerPanel::RegisterCommands(Core::CommandRegistry& commands)
	{
		for (const DisplayMode& mode : kDisplayModes)
		{
			commands.Add({.Id = mode.Id,
						  .Label = mode.Label,
						  .ListLabel = mode.ListLabel,
						  .Run = [this, mode] { Apply(mode.Display, true); },
						  .Checked = [this, mode] { return m_Display == mode.Display; }});
		}

		commands.Add({.Id = "explorer.followActiveFile",
					  .Label = "Follow Active File",
					  .Run = [this] { ApplyFollowsActiveFile(!m_FollowsActiveFile, true); },
					  .Checked = [this] { return m_FollowsActiveFile; }});
	}

	void ExplorerPanel::Apply(GitIgnoredDisplay display, bool persist)
	{
		m_Display = display;
		m_Files.SetGitIgnoredDisplay(display);

		if (persist)
		{
			m_AppSettings.SetGitIgnoredDisplay(GitIgnoredDisplayToString(display));
		}
	}

	void ExplorerPanel::ApplyFollowsActiveFile(bool follow, bool persist)
	{
		m_FollowsActiveFile = follow;
		m_Files.SetFollowsActiveFile(follow);
		m_Solution.SetFollowsActiveFile(follow);

		if (persist)
		{
			m_AppSettings.SetExplorerFollowsActiveFile(follow);
		}
	}
}