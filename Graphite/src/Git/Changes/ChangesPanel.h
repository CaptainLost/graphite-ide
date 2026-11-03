#pragma once

#include "UI/Dialogs/ModalGate.h"
#include "Git/Changes/ChangesShared.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Diff/DiffWindows.h"
#include "Git/GitModel.h"
#include "Theme/ActiveScheme.h"

#include "Git/EditorBuffers.h"

#include "Core/UI/Panel.h"

namespace Core {
	class EventBus;
}

namespace Graphite {

	// The side panel's host of the Changes view, which also does what must happen exactly once per write,
	// whichever host made it.
	class ChangesPanel final : public Core::Panel
	{
	public:
		ChangesPanel(GitModel& model, ModalGate& modals, EditorBuffers& editor, const ActiveScheme& scheme,
					 DiffWindows& diffWindows, Core::EventBus& events, ChangesShared& shared);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		GitModel& m_Model;

		EditorBuffers& m_Editor;

		DiffWindows& m_DiffWindows;

		ChangesShared& m_Shared;

		ChangesView m_View;
	};
}
