#include "Settings/Panel/EditorPage.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/CommandControls.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/ImGui/Toggle.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace Graphite {

	namespace {

		constexpr float kMinimumFontSize = 8.0f;
		constexpr float kMaximumFontSize = 32.0f;
		constexpr int kMinimumTabWidth = 1;
		constexpr int kMaximumTabWidth = 8;
		constexpr int kMinimumHoverDelayMs = 200;
		constexpr int kMaximumHoverDelayMs = 1500;

		const std::vector<std::string> kIndentOptions{"Spaces", "Tabs"};
		constexpr std::size_t kSpacesOption = 0;
		constexpr std::size_t kTabsOption = 1;

		const std::vector<std::string> kPreviewOptions{"Off", "With Ctrl", "Always"};
		constexpr std::array<DefinitionPreviewMode, 3> kPreviewModes{DefinitionPreviewMode::Off, DefinitionPreviewMode::WithCtrl,
																	 DefinitionPreviewMode::Always};

		const std::vector<std::string> kGitKeywords{"git", "diff"};

		std::size_t PreviewOptionOf(DefinitionPreviewMode mode)
		{
			for (std::size_t optionIndex = 0; optionIndex < kPreviewModes.size(); ++optionIndex)
			{
				if (kPreviewModes[optionIndex] == mode)
				{
					return optionIndex;
				}
			}

			return kPreviewModes.size();
		}
	}

	EditorPage::EditorPage(EditorSettings& settings, Core::CommandRegistry& commands) : m_Settings(settings), m_Commands(commands)
	{
	}

	const char* EditorPage::GetIcon() const
	{
		return ICON_MDI_PENCIL_OUTLINE;
	}

	void EditorPage::OnDeactivated()
	{
		m_PendingFontSize.reset();
		m_PendingHoverDelay.reset();
	}

	std::vector<SettingsCard> EditorPage::Describe()
	{
		return {DescribeText(), DescribeIndentation(), DescribeHover(), DescribeGitChanges()};
	}

	SettingsCard EditorPage::DescribeText()
	{
		SettingsCard card{.Heading = "Text"};

		card.Rows.push_back(SettingsRow{.Title = "Font size",
										.Description = "Size of code text. The interface has its own scale, under Appearance.",
										.Keywords = {"zoom", "font"},
										.ControlWidth = SettingsLayout::SliderWidth(),
										.DrawControl = [this] { DrawFontSize(); }});
		card.Rows.push_back(SettingsRow{.Title = "Word wrap",
										.Description = "Long lines continue on the next line instead of running off to the side.",
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawWordWrap(); }});

		return card;
	}

	SettingsCard EditorPage::DescribeIndentation()
	{
		SettingsCard card{.Heading = "Indentation"};

		card.Rows.push_back(SettingsRow{.Title = "Indent with",
										.Description = "What Tab inserts and Outdent removes.",
										.Keywords = {"spaces", "tabs"},
										.ControlWidth = SettingsLayout::ComboWidth(kIndentOptions),
										.DrawControl = [this] { DrawIndentWith(); }});
		card.Rows.push_back(SettingsRow{.Title = "Tab width",
										.Description = "How wide a tab is drawn, and how many spaces an indent inserts.",
										.Keywords = {"indent"},
										.ControlWidth = SettingsLayout::NumberWidth(),
										.DrawControl = [this] { DrawTabWidth(); }});

		return card;
	}

	SettingsCard EditorPage::DescribeHover()
	{
		SettingsCard card{.Heading = "Hover"};

		card.Rows.push_back(SettingsRow{.Title = "Symbol information",
										.Description = "Shows a symbol's type and documentation when the pointer rests on it.",
										.Keywords = {"tooltip"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawSymbolInformation(); }});
		card.Rows.push_back(SettingsRow{.Title = "Definition preview",
										.Description = "Shows the code where the symbol is defined.",
										.Keywords = {"tooltip", "peek"},
										.ControlWidth = SettingsLayout::ComboWidth(kPreviewOptions),
										.DrawControl = [this] { DrawDefinitionPreview(); }});
		card.Rows.push_back(SettingsRow{.Title = "Hover delay",
										.Description = "How long the pointer rests before either appears.",
										.Keywords = {"tooltip"},
										.ControlWidth = SettingsLayout::SliderWidth(),
										.DrawControl = [this] { DrawHoverDelay(); }});

		return card;
	}

	SettingsCard EditorPage::DescribeGitChanges()
	{
		SettingsCard card{.Heading = "Git changes"};

		card.Rows.push_back(SettingsRow{.Title = "Markers in the gutter",
										.Description = "Coloured bars beside the lines added, changed or removed since the last commit.",
										.Keywords = kGitKeywords,
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawGutterMarkers(); }});
		card.Rows.push_back(SettingsRow{.Title = "Markers on the scrollbar",
										.Description = "The same changes, marked along the scrollbar.",
										.Keywords = kGitKeywords,
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawScrollbarMarkers(); }});

		return card;
	}

	void EditorPage::DrawFontSize()
	{
		float fontSize = m_PendingFontSize.value_or(m_Settings.GetFontSize());

		if (ImGui::SliderFloat("##fontSize", &fontSize, kMinimumFontSize, kMaximumFontSize, "%.1f"))
		{
			m_PendingFontSize = fontSize;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingFontSize.has_value())
		{
			m_Settings.SetFontSize(*m_PendingFontSize);
			m_PendingFontSize.reset();
		}
	}

	void EditorPage::DrawWordWrap()
	{
		DrawCommandToggle(m_Commands, "editor.toggleWordWrap");
	}

	void EditorPage::DrawIndentWith()
	{
		std::size_t selected = m_Settings.GetIndentStyle().UseSpaces ? kSpacesOption : kTabsOption;

		if (SettingsLayout::Choice("##indentWith", kIndentOptions, selected))
		{
			m_Settings.SetIndentWithSpaces(selected == kSpacesOption);
		}
	}

	// An input rather than a slider: the grab of a slider this short covers the number. A typed value
	// is not bounded by the widget, so it is clamped here.
	void EditorPage::DrawTabWidth()
	{
		int width = static_cast<int>(m_Settings.GetIndentStyle().Width);

		if (ImGui::InputInt("##tabWidth", &width))
		{
			m_Settings.SetIndentWidth(static_cast<std::size_t>(std::clamp(width, kMinimumTabWidth, kMaximumTabWidth)));
		}
	}

	void EditorPage::DrawSymbolInformation()
	{
		bool show = m_Settings.GetHoverSettings().ShowInfo;

		if (Core::Toggle("##symbolInformation", &show))
		{
			m_Settings.SetHoverInfo(show);
		}
	}

	void EditorPage::DrawDefinitionPreview()
	{
		std::size_t selected = PreviewOptionOf(m_Settings.GetHoverSettings().Preview);

		if (SettingsLayout::Choice("##definitionPreview", kPreviewOptions, selected))
		{
			m_Settings.SetDefinitionPreview(kPreviewModes[selected]);
		}
	}

	void EditorPage::DrawHoverDelay()
	{
		int delay = m_PendingHoverDelay.value_or(m_Settings.GetHoverSettings().DelayMs);

		if (ImGui::SliderInt("##hoverDelay", &delay, kMinimumHoverDelayMs, kMaximumHoverDelayMs, "%d ms"))
		{
			m_PendingHoverDelay = delay;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_PendingHoverDelay.has_value())
		{
			m_Settings.SetHoverDelayMs(*m_PendingHoverDelay);
			m_PendingHoverDelay.reset();
		}
	}

	void EditorPage::DrawGutterMarkers()
	{
		bool show = m_Settings.HasGutterChangeMarkers();

		if (Core::Toggle("##gutterMarkers", &show))
		{
			m_Settings.SetGutterChangeMarkers(show);
		}
	}

	void EditorPage::DrawScrollbarMarkers()
	{
		bool show = m_Settings.HasScrollbarChangeMarkers();

		if (Core::Toggle("##scrollbarMarkers", &show))
		{
			m_Settings.SetScrollbarChangeMarkers(show);
		}
	}
}
