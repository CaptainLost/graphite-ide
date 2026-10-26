#include "Explorer/ExplorerPanel.h"

#include "Settings/AppSettings.h"
#include "UI/PanelHeader.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/UI/PanelMarginScope.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kModeKey = "explorerMode";
		constexpr const char* kSolutionChoiceId = "solutionChoice";
		constexpr const char* kFileSystemLabel = "File System";
		constexpr const char* kSolutionLabel = "Visual Studio Solution";
		constexpr std::size_t kModeChoice = 0;
		constexpr std::size_t kSolutionChoice = 1;
		constexpr int kFileSystemOption = 0;
		constexpr int kSolutionOption = 1;
		constexpr int kNoOption = -1;
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

		// WARNING: Follows the solution chosen, not the one read: the read runs on a worker, and the choice would show
		// nothing chosen until it lands.
		PanelHeaderChoice SolutionChoice(const std::vector<std::filesystem::path>& candidates,
										 const std::filesystem::path& selected)
		{
			PanelHeaderChoice choice{.Id = kSolutionChoiceId, .Options = {}, .Selected = kNoOption};

			for (std::size_t index = 0; index < candidates.size(); ++index)
			{
				choice.Options.push_back(Core::ToUtf8(candidates[index].filename()));

				if (candidates[index] == selected)
				{
					choice.Selected = static_cast<int>(index);
				}
			}

			return choice;
		}
	}

	ExplorerPanel::ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents,
								 AppSettings& settings, Core::EventBus& events, ModalGate& modals,
								 const ActiveScheme& scheme, Core::JobSubmitter submitJob)
		: Core::Panel("files", "Explorer", "navigate", ICON_LC_FOLDER_TREE, StartsAs::Visible, Sides::Flush),
		  m_Workspace(workspace),
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
		m_Files.ApplyFinishedOperations();

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

		{
			const Core::PanelMarginScope margin;
			RenderHeader();
		}

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

	std::optional<std::string> ExplorerPanel::FileOperationText() const
	{
		return m_Files.FileOperationText();
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
		const std::vector<std::filesystem::path>& candidates = m_Workspace.GetSolutionCandidates();
		const bool choosesSolution = m_Mode == Mode::Solution && candidates.size() > 1;

		std::vector<PanelHeaderChoice> choices{
			PanelHeaderChoice{.Id = kModeKey,
							  .Options = {kFileSystemLabel, kSolutionLabel},
							  .Selected = m_Mode == Mode::Files ? kFileSystemOption : kSolutionOption}};

		if (choosesSolution)
		{
			choices.push_back(SolutionChoice(candidates, m_Workspace.GetSelectedSolution()));
		}

		const std::vector<HeaderCommand> commands = HeaderCommands();
		std::vector<PanelHeaderAction> actions;
		std::ranges::transform(commands, std::back_inserter(actions), &ExplorerPanel::ActionFor);

		const PanelHeaderResult result = DrawPanelHeader(choices, actions);

		if (result.ClickedAction.has_value())
		{
			Run(commands[*result.ClickedAction]);
		}

		if (const std::optional<int> chosen = result.Chosen[kSolutionChoice]; choosesSolution && chosen.has_value())
		{
			m_Workspace.SelectSolution(candidates[static_cast<std::size_t>(*chosen)]);
		}

		if (const std::optional<int> chosen = result.Chosen[kModeChoice]; chosen.has_value())
		{
			SetMode(*chosen == kSolutionOption ? Mode::Solution : Mode::Files);
		}
	}

	PanelHeaderAction ExplorerPanel::ActionFor(HeaderCommand command)
	{
		switch (command)
		{
		case HeaderCommand::NewFile:
			return {.Id = "##newFile", .Glyph = ICON_LC_FILE_PLUS, .Tooltip = "New File"};
		case HeaderCommand::NewFolder:
			return {.Id = "##newFolder", .Glyph = ICON_LC_FOLDER_PLUS, .Tooltip = "New Folder"};
		case HeaderCommand::NewSolutionFolder:
			return {.Id = "##newSolutionFolder", .Glyph = ICON_LC_FOLDER_PLUS, .Tooltip = "New Folder"};
		case HeaderCommand::Refresh:
			return {.Id = "##refresh", .Glyph = ICON_LC_REFRESH_CW, .Tooltip = "Refresh"};
		}

		return {.Id = "##refresh", .Glyph = ICON_LC_REFRESH_CW, .Tooltip = "Refresh"};
	}

	std::vector<ExplorerPanel::HeaderCommand> ExplorerPanel::HeaderCommands() const
	{
		if (!m_Workspace.IsOpen())
		{
			return {};
		}

		if (m_Mode == Mode::Files)
		{
			return {HeaderCommand::NewFile, HeaderCommand::NewFolder, HeaderCommand::Refresh};
		}

		if (m_Solution.CanEditSolution())
		{
			return {HeaderCommand::NewSolutionFolder, HeaderCommand::Refresh};
		}

		return {HeaderCommand::Refresh};
	}

	void ExplorerPanel::Run(HeaderCommand command)
	{
		switch (command)
		{
		case HeaderCommand::NewFile:
			m_Files.NewFileInRoot();
			return;
		case HeaderCommand::NewFolder:
			m_Files.NewFolderInRoot();
			return;
		case HeaderCommand::NewSolutionFolder:
			m_Solution.NewFolderInRoot();
			return;
		case HeaderCommand::Refresh:
			m_Files.Refresh();
			return;
		}
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