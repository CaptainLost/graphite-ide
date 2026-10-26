#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Explorer/FileTreeView.h"
#include "Explorer/SolutionTreeView.h"
#include "Workspace/Workspace.h"

#include "Core/Jobs/JobSubmitter.h"
#include "Core/UI/Panel.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

	class CommandRegistry;
	class EventBus;

}

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class AppSettings;
	struct PanelHeaderAction;

	class ExplorerPanel final : public Core::Panel
	{
	public:
		ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents, AppSettings& settings,
					  Core::EventBus& events, ModalGate& modals, const ActiveScheme& scheme, Core::JobSubmitter submitJob);

		void OnUpdate(Core::Timestep timestep) override;

		void OnImGuiRender() override;

		void LoadSettings();

		void RegisterCommands(Core::CommandRegistry& commands);

		void SetGitDecorations(const GitDecorationView& decorations);

		void SetShownFile(const std::filesystem::path& path);

		std::optional<std::string> FileOperationText() const;

	private:
		enum class Mode
		{
			Files,
			Solution
		};

		enum class HeaderCommand
		{
			NewFile,
			NewFolder,
			NewSolutionFolder,
			Refresh
		};

		static Mode ModeFromSetting(const std::string& value);
		static const char* SettingForMode(Mode mode);

		static PanelHeaderAction ActionFor(HeaderCommand command);

		void RenderHeader();
		std::vector<HeaderCommand> HeaderCommands() const;
		void Run(HeaderCommand command);
		void SetMode(Mode mode);

		void SyncModeWithWorkspace();

		void Apply(GitIgnoredDisplay display, bool persist);
		void ApplyFollowsActiveFile(bool follow, bool persist);

		Workspace& m_Workspace;
		AppSettings& m_AppSettings;
		FileTreeView m_Files;
		SolutionTreeView m_Solution;
		Mode m_Mode = Mode::Files;

		std::filesystem::path m_ModeLoadedFor;

		GitIgnoredDisplay m_Display = GitIgnoredDisplay::Dim;
		bool m_FollowsActiveFile = true;
	};
}