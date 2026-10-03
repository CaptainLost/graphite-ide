#pragma once

#include "UI/ModalQuestion.h"
#include "Settings/SettingsSection.h"
#include "Theme/SchemeDraft.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace Graphite {

	class ActiveScheme;
	class AppSettings;

	// Chooses the editor's colour scheme and edits the ones that are not built in. Holds a
	// SchemeDraft for every decision and keeps only what ImGui needs on top of it.
	class SchemeSection : public SettingsSection
	{
	public:
		SchemeSection(ActiveScheme& scheme, AppSettings& settings, ModalGate& modals);

		std::string_view GetTitle() const override { return "Colours"; }

		void OnImGuiRender() override;

		// Re-resolves from the saved setting, so a scheme abandoned part-way cannot leave the
		// editor painted from something that was never written down.
		void OnDeactivated() override;

	private:
		void Apply();

		void RenderSchemeList();

		// Templated over the enum so the two groups share one row, rather than a syntax copy and
		// an editor copy drifting apart the way a key table and a label table would.
		template<typename Which>
			requires std::is_enum_v<Which>
		void RenderColorRow(Which which);

		template<typename Which>
			requires std::is_enum_v<Which>
		void RenderGroup(const char* title, std::size_t count);

		ActiveScheme& m_Active;
		AppSettings& m_Settings;

		SchemeDraft m_Draft;

		ModalQuestion<std::string> m_DeleteQuestion;
	};
}
