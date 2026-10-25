#pragma once

#include "Settings/Panel/SettingsPage.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Core {

	class CommandRegistry;
}

namespace Graphite {

	class EditorSettings;

	class EditorPage final : public SettingsPage
	{
	public:
		EditorPage(EditorSettings& settings, Core::CommandRegistry& commands);

		std::string_view GetId() const override { return "editor"; }

		std::string_view GetTitle() const override { return "Editor"; }

		const char* GetIcon() const override;

		std::vector<SettingsCard> Describe() override;

		void OnDeactivated() override;

	private:
		SettingsCard DescribeText();
		SettingsCard DescribeIndentation();
		SettingsCard DescribeHover();
		SettingsCard DescribeGitChanges();

		void DrawFontSize();
		void DrawWordWrap();
		void DrawHighlightOccurrences();
		void DrawIndentWith();
		void DrawTabWidth();
		void DrawSymbolInformation();
		void DrawDefinitionPreview();
		void DrawHoverDelay();
		void DrawGutterMarkers();
		void DrawScrollbarMarkers();
		void DrawLineAuthor();

		EditorSettings& m_Settings;
		Core::CommandRegistry& m_Commands;

		std::optional<float> m_FontSizeWhileDragged;
		std::optional<int> m_HoverDelayWhileDragged;
	};
}
