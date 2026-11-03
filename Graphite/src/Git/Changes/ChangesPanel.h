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

	// The side panel's host of the Changes view. What a landed write changes elsewhere is WriteConsequences' job.
	class ChangesPanel final : public Core::Panel
	{
	public:
		ChangesPanel(GitModel& model, ModalGate& modals, EditorBuffers& editor, const ActiveScheme& scheme,
					 DiffWindows& diffWindows, Core::EventBus& events, ChangesShared& shared);

		void OnUpdate(Core::Timestep ts) override;
		void OnImGuiRender() override;

	private:
		DiffWindows& m_DiffWindows;

		ChangesView m_View;
	};
}
