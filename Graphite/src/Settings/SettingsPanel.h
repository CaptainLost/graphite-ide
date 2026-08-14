#pragma once

#include "Core/Base/Base.h"
#include "Core/UI/Panel.h"
#include "Settings/SettingsSection.h"

#include <cstddef>
#include <vector>

namespace Graphite {

	class SettingsPanel final : public Core::Panel
	{
	public:
		explicit SettingsPanel(std::vector<Core::Scope<SettingsSection>> sections);

		void OnImGuiRender() override;

		// Runs whether or not the panel is drawn, which is what makes it the place to notice
		// that it stopped being drawn.
		void OnUpdate(Core::Timestep ts) override;

		void OnEvent(Core::Event& event) override;

		// Ignores an index past the end, so a command carrying a section that no longer exists
		// leaves the panel on whatever it was showing rather than indexing off the vector.
		void SelectSection(std::size_t index);

		std::size_t GetActiveSection() const { return m_Active; }

	private:
		std::vector<Core::Scope<SettingsSection>> m_Sections;
		std::size_t m_Active = 0;
		bool m_WasDrawnLastFrame = false;
	};
}
