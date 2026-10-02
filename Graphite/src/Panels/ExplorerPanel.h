#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Panels/FileTreeView.h"
#include "Panels/SolutionTreeView.h"
#include "Workspace/Workspace.h"

#include "Core/UI/Panel.h"

#include <filesystem>
#include <string>

namespace Core {

	class CommandRegistry;

}

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class AppSettings;

	class ExplorerPanel final : public Core::Panel
	{
	public:
		ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents, AppSettings& settings);

		void OnImGuiRender() override;

		void LoadSettings();

		void RegisterCommands(Core::CommandRegistry& commands);

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