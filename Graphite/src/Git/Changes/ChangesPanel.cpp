#include "Git/Changes/ChangesPanel.h"

#include <IconsLucide.h>

#include <filesystem>
#include <map>
#include <vector>

namespace Graphite {

	ChangesPanel::ChangesPanel(GitModel& model, ModalGate& modals, EditorBuffers& editor, const ActiveScheme& scheme,
							   DiffWindows& diffWindows, Core::EventBus& events, ChangesShared& shared)
		: Core::Panel("changes", "Changes", "git", ICON_LC_FILE_DIFF, StartsAs::Visible, Sides::Flush),
		  m_Model(model), m_Editor(editor), m_DiffWindows(diffWindows), m_Shared(shared),
		  m_View(model, modals, editor, scheme, events, shared, "panel")
	{
		m_View.OnFileChosen = [this](const OpenDiff& diff) {
			// WARNING: GitModel keeps a single slot for the working-tree diff, so two tabs of
			// it would take turns failing. Closed only for a different diff: Open() re-docks an
			// exact match, and closing first would yank a tab the user moved back to the centre.
			if (!m_DiffWindows.IsOpen(diff))
			{
				m_DiffWindows.CloseWorkingTree();
			}

			m_DiffWindows.Open(diff);
		};
	}

	// WARNING: PanelManager calls OnUpdate for every panel every frame but skips OnImGuiRender
	// for a hidden, collapsed or tabbed-behind panel. A question left open while the panel is not
	// drawn would hold the modal gate forever, so it is withdrawn here. TakeCommitted and
	// TakeDiscarded are drained here for the same reason: their expectation must be registered
	// within milliseconds of the write that produced it. Each answers once, so this panel is the
	// only host that drains them, for writes made from any host.
	void ChangesPanel::OnUpdate(Core::Timestep)
	{
		if (!HasBeenDrawnOnLatestFrame())
		{
			m_View.WithdrawQuestions();
		}

		if (m_Model.TakeCommitted().has_value())
		{
			m_Shared.Message.clear();
			m_Shared.Amend = false;
		}

		if (std::vector<std::filesystem::path> discarded = m_Model.TakeDiscarded(); !discarded.empty())
		{
			if (m_Model.GetWorkingDirectory().empty())
			{
				return;
			}

			// WARNING: Status paths are relative to GetWorkingDirectory(), not GetRoot():
			// Repository::Open searches upward for a .git, so the two can differ. A discarded
			// directory is expanded into the files remembered in the shared DiscardedFiles, because
			// the editor holds documents and the directory is already gone from disk.
			std::vector<std::filesystem::path> absolutePaths;
			absolutePaths.reserve(discarded.size());

			for (const std::filesystem::path& discardedPath : discarded)
			{
				using DiscardedFilesMap =
					std::map<std::filesystem::path, std::vector<std::filesystem::path>>;

				DiscardedFilesMap::node_type rememberedEntry = m_Shared.DiscardedFiles.extract(discardedPath);

				if (rememberedEntry.empty())
				{
					absolutePaths.push_back(m_Model.GetWorkingDirectory() / discardedPath);
					continue;
				}

				for (const std::filesystem::path& rememberedFile : rememberedEntry.mapped())
				{
					absolutePaths.push_back(m_Model.GetWorkingDirectory() / rememberedFile);
				}
			}

			m_Editor.ExpectExternalWrite(absolutePaths);
		}
	}

	void ChangesPanel::OnImGuiRender()
	{
		m_View.Draw();
	}
}
