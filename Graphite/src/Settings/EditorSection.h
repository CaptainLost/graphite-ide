#pragma once

#include "Settings/SettingsSection.h"

#include <optional>
#include <string_view>

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

		// The slider's working value while it is being dragged. Committed to m_Settings only on
		// ImGui::IsItemDeactivatedAfterEdit, so a drag writes graphite.json once instead of once
		// per frame. Empty when no drag is in progress.
		std::optional<float> m_PendingFontSize;
		std::optional<int> m_PendingIndentWidth;
		std::optional<int> m_PendingHoverDelay;
	};
}
