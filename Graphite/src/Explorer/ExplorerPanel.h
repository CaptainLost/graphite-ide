#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Explorer/FileTreeView.h"
#include "Explorer/SolutionTreeView.h"
#include "Workspace/Workspace.h"

#include "Core/UI/Panel.h"

#include <filesystem>
#include <string>

namespace Core {

	class CommandRegistry;
	class EventBus;

}

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class AppSettings;

	class ExplorerPanel final : public Core::Panel
	{
	public:
		ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents, AppSettings& settings,
					  Core::EventBus& events);

		void OnImGuiRender() override;

		void LoadSettings();

		void RegisterCommands(Core::CommandRegistry& commands);

		// This frame's git status for the trees, set before the panel renders.
		void SetGitDecorations(const GitDecorationView& decorations);

	private:
		enum class Mode
		{
			Files,
			Solution
		};

		static Mode ModeFromSetting(const std::string& value);
		static const char* SettingForMode(Mode mode);

		void RenderHeader();
		void RenderModeCombo();
		void RenderSolutionChooser();
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