#pragma once

#include "Workspace/GitIgnoredDisplay.h"
#include "Panels/FileTreeView.h"
#include "Panels/SolutionTreeView.h"
#include "Workspace/Workspace.h"

#include "Core/UI/Panel.h"

namespace Core {

	class CommandRegistry;

}

namespace Core {

	class DocumentSet;

}

namespace Graphite {

	class ExplorerPanel final : public Core::Panel
	{
	public:
		ExplorerPanel(Workspace& workspace, const Core::DocumentSet& documents);

		void OnImGuiRender() override;

		void LoadSettings();

		void RegisterCommands(Core::CommandRegistry& commands);

	private:
		enum class Mode
		{
			Files,
			Solution
		};

		void RenderHeader();
		void SetMode(Mode mode);

		void SyncModeWithWorkspace();

		void Apply(GitIgnoredDisplay display, bool persist);

		Workspace& m_Workspace;
		FileTreeView m_Files;
		SolutionTreeView m_Solution;
		Mode m_Mode = Mode::Files;

		std::filesystem::path m_ModeLoadedFor;

		GitIgnoredDisplay m_Display = GitIgnoredDisplay::Dim;
	};
}