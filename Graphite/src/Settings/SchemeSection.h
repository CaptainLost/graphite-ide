#pragma once

#include "Settings/SettingsSection.h"
#include "Theme/SchemeDraft.h"

#include <cstddef>
#include <string>

namespace Graphite {

	class ActiveScheme;

	// Chooses the editor's colour scheme and edits the ones that are not built in. Holds a
	// SchemeDraft for every decision and keeps only what ImGui needs on top of it.
	class SchemeSection : public SettingsSection
	{
	public:
		explicit SchemeSection(ActiveScheme& scheme);

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
		void RenderColorRow(Which which);

		template<typename Which>
		void RenderGroup(const char* title, std::size_t count);

		ActiveScheme& m_Active;

		SchemeDraft m_Draft;
	};
}
