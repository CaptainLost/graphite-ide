#pragma once

#include "Settings/SettingsSection.h"

namespace Core {

	class CommandRegistry;
}

namespace Graphite {

	class EditorSettings;

	class EditorSection final : public SettingsSection
	{
	public:
		EditorSection(EditorSettings& settings, Core::CommandRegistry& commands);

		std::string_view GetTitle() const override { return "Editor"; }

		void OnImGuiRender() override;

	private:
		void RenderCommandCheckbox(const char* label, const char* id);

		void RenderCommandRadio(const char* label, const char* id);

		EditorSettings& m_Settings;
		Core::CommandRegistry& m_Commands;
	};
}
