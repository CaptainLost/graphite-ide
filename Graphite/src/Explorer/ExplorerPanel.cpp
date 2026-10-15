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
		constexpr float kFillAvailableWidth = -1.0f;
		constexpr bool kPersist = true;
		constexpr bool kDoNotPersist = false;

		struct DisplayMode
		{
			const char* Id;
			const char* Label;
			const char* ListLabel;
			GitIgnoredDisplay Display;
		};

		constexpr DisplayMode kDisplayModes[] = {
			{"git.ignored.show", "Show", "Ignored Files: Show", GitIgnoredDisplay::Show},
			{"git.ignored.dim", "Dim", "Ignored Files: Dim", GitIgnoredDisplay::Dim},
			{"git.ignored.hide", "Hide", "Ignored Files: Hide", GitIgnoredDisplay::Hide},
		};
	}

	ExplorerPanel::ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents,
								 AppSettings& settings, Core::EventBus& events, ModalGate& modals,
								 const ActiveScheme& scheme, Core::JobSubmitter submitJob)
		: Core::Panel("files", "Explorer", "navigate", ICON_MDI_FILE_TREE), m_Workspace(workspace),
		  m_AppSettings(settings), m_Files(workspace, documents, events, modals, scheme, std::move(submitJob)),
		  m_Solution(workspace, documents, events, modals)
	{
		// WARNING: The stored mode is read when the panel first sees a folder and would put a
		// just-opened solution back behind the file tree, so the folder is marked as seen here.
		events.Subscribe<SolutionOpenedEvent>([this](const SolutionOpenedEvent&) {
			m_ModeLoadedFor = m_Workspace.GetRoot();
			SetMode(Mode::Solution);
		});
	}

	// WARNING: Begin hands the modal gate back, and it only runs for the view drawn. A question left in
	// a hidden panel, or in the view the mode switched away from, would hold the gate for good.
	void ExplorerPanel::OnUpdate(Core::Timestep)
	{
		const bool drawn = HasBeenDrawnOnLatestFrame();

		if (!drawn || m_Mode != Mode::Files)
		{
			m_Files.WithdrawQuestions();
		}

		if (!drawn || m_Mode != Mode::Solution)
		{
			m_Solution.WithdrawQuestions();
		}
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

	void ExplorerPanel::SetShownFile(const std::filesystem::path& path)
	{
		m_Files.SetShownFile(path);
		m_Solution.SetShownFile(path);
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
		const char* currentLabel = m_Mode == Mode::Files ? kFileSystemLabel : kSolutionLabel;

		ImGui::SetNextItemWidth(kFillAvailableWidth);

		if (ImGui::BeginCombo("##explorerMode", currentLabel))
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
		// WARNING: This must be a value, not a reference: the empty branch is a temporary.
		const std::filesystem::path loaded =
			m_Workspace.GetLoadedSolution() ? m_Workspace.GetLoadedSolution()->Path : std::filesystem::path{};

		ImGui::SetNextItemWidth(kFillAvailableWidth);

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
		Apply(GitIgnoredDisplayFromString(m_AppSettings.GetGitIgnoredDisplay()), kDoNotPersist);
		ApplyFollowsActiveFile(m_AppSettings.IsExplorerFollowingActiveFile(), kDoNotPersist);
	}

	void ExplorerPanel::RegisterCommands(Core::CommandRegistry& commands)
	{
		for (const DisplayMode& displayMode : kDisplayModes)
		{
			commands.Add({.Id = displayMode.Id,
						  .Label = displayMode.Label,
						  .ListLabel = displayMode.ListLabel,
						  .Run = [this, displayMode] { Apply(displayMode.Display, kPersist); },
						  .Checked = [this, displayMode] { return m_Display == displayMode.Display; }});
		}

		commands.Add({.Id = "explorer.followActiveFile",
					  .Label = "Follow Active File",
					  .Run = [this] { ApplyFollowsActiveFile(!m_FollowsActiveFile, kPersist); },
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