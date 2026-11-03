#include "Git/Changes/ChangesPanel.h"

#include <IconsLucide.h>

namespace Graphite {

	ChangesPanel::ChangesPanel(GitModel& model, ModalGate& modals, EditorBuffers& editor, const ActiveScheme& scheme,
							   DiffWindows& diffWindows, Core::EventBus& events, ChangesShared& shared)
		: Core::Panel("changes", "Changes", "git", ICON_LC_FILE_DIFF, StartsAs::Visible, Sides::Flush),
		  m_DiffWindows(diffWindows), m_View(model, modals, editor, scheme, events, shared, "panel")
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
	// drawn would hold the modal gate forever, so it is withdrawn here.
	void ChangesPanel::OnUpdate(Core::Timestep)
	{
		if (!HasBeenDrawnOnLatestFrame())
		{
			m_View.WithdrawQuestions();
		}
	}

	void ChangesPanel::OnImGuiRender()
	{
		m_View.Draw();
	}
}
