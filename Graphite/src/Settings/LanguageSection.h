#pragma once

#include "Settings/SettingsSection.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	class AnalysisSettings;
	class AppSettings;

	// The settings screen for everything about language servers: the C++ server's path and the
	// two background-analysis values. The other languages' paths belong here too, which is why
	// this is its own section rather than two more rows under Editor.
	class LanguageSection final : public SettingsSection
	{
	public:
		LanguageSection(AnalysisSettings& settings, AppSettings& store, std::function<void()> onCppServerChanged);

		std::string_view GetTitle() const override { return "Language"; }

		void OnImGuiRender() override;

	private:
		void RenderServerPath();

		AnalysisSettings& m_Settings;
		AppSettings& m_Store;
		std::function<void()> m_OnCppServerChanged;

		// The path field's text while it is being edited, so the stored value does not overwrite
		// what is being typed on the next frame.
		std::string m_PendingServerPath;
		bool m_EditingServerPath = false;

		// The slider's working value while it is being dragged. Committed to m_Settings only on
		// ImGui::IsItemDeactivatedAfterEdit, so a drag writes graphite.json once instead of once
		// per frame. Empty when no drag is in progress. Same reason EditorSection's font-size and
		// tab-width sliders hold their own pending value.
		std::optional<int> m_PendingBudgetMegabytes;
	};
}
