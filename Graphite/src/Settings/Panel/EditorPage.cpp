#include "Settings/Panel/EditorPage.h"

#include "Editor/EditorSettings.h"
#include "Settings/Panel/CommandControls.h"
#include "Settings/Panel/SettingsLayout.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/ImGui/Toggle.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

namespace Graphite {

	namespace {

		constexpr int kLineAuthorDelayStepMs = 100;

		const std::vector<std::string> kIndentOptions{"Spaces", "Tabs"};
		constexpr std::size_t kSpacesOption = 0;
		constexpr std::size_t kTabsOption = 1;

		const std::vector<std::string> kLineEndingOptions{"LF", "CRLF"};
		constexpr std::size_t kLfOption = 0;
		constexpr std::size_t kCrlfOption = 1;

		const std::vector<std::string> kPreviewOptions{"Off", "With Ctrl", "Always"};
		constexpr std::array<DefinitionPreviewMode, 3> kPreviewModes{DefinitionPreviewMode::Off, DefinitionPreviewMode::WithCtrl,
																	 DefinitionPreviewMode::Always};

		const std::vector<std::string> kGitKeywords{"git", "diff"};
		const std::vector<std::string> kLineAuthorKeywords{"git", "diff", "blame"};
		const std::vector<std::string> kLineAuthorDelayKeywords{"git", "diff", "blame", "delay"};

		int ToNearestStep(int value, int step)
		{
			return (value + step / 2) / step * step;
		}

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
		return ICON_LC_PENCIL;
	}

	void EditorPage::OnDeactivated()
	{
		m_FontSizeWhileDragged.reset();
		m_HoverDelayWhileDragged.reset();
		m_LineAuthorDelayWhileDragged.reset();
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
		card.Rows.push_back(SettingsRow{.Title = "Save when Graphite loses focus",
										.Description = "Every modified file is saved as you switch to another application.",
										.Keywords = {"auto save", "autosave", "focus"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawCommandToggle(m_Commands, "editor.toggleSaveOnFocusLoss"); }});
		card.Rows.push_back(SettingsRow{.Title = "Highlight occurrences",
										.Description = "Selecting a whole word marks everywhere else it appears in the file, in the text and beside the scrollbar.",
										.Keywords = {"selection", "word", "matches"},
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawHighlightOccurrences(); }});

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
		card.Rows.push_back(SettingsRow{.Title = "Line ending of new files",
										.Description = "What a new file puts between its lines. A file that has line breaks keeps its own.",
										.Keywords = {"eol", "lf", "crlf", "newline"},
										.ControlWidth = SettingsLayout::ComboWidth(kLineEndingOptions),
										.DrawControl = [this] { DrawNewFileLineEnding(); }});

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
		card.Rows.push_back(SettingsRow{.Title = "Line author",
										.Description = "Show who last changed the line with the caret, at its end.",
										.Keywords = kLineAuthorKeywords,
										.ControlWidth = SettingsLayout::ToggleWidth(),
										.DrawControl = [this] { DrawLineAuthor(); }});
		card.Rows.push_back(SettingsRow{.Title = "Line author delay",
										.Description = "How long the caret rests on a line before its author shows. 0 shows it at once.",
										.Keywords = kLineAuthorDelayKeywords,
										.ControlWidth = SettingsLayout::SliderWidth(),
										.DrawControl = [this] { DrawLineAuthorDelay(); }});

		return card;
	}

	void EditorPage::DrawFontSize()
	{
		float fontSize = m_FontSizeWhileDragged.value_or(m_Settings.GetFontSize());

		if (ImGui::SliderFloat("##fontSize", &fontSize, EditorSettings::kMinimumFontSize, EditorSettings::kMaximumFontSize, "%.1f"))
		{
			m_FontSizeWhileDragged = fontSize;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_FontSizeWhileDragged.has_value())
		{
			m_Settings.SetFontSize(*m_FontSizeWhileDragged);
			m_FontSizeWhileDragged.reset();
		}
	}

	void EditorPage::DrawWordWrap()
	{
		DrawCommandToggle(m_Commands, "editor.toggleWordWrap");
	}

	void EditorPage::DrawHighlightOccurrences()
	{
		bool highlight = m_Settings.IsHighlightingOccurrences();

		if (Core::Toggle("##highlightOccurrences", &highlight))
		{
			m_Settings.SetHighlightOccurrences(highlight);
		}
	}

	void EditorPage::DrawIndentWith()
	{
		std::size_t selected = m_Settings.GetIndentStyle().UseSpaces ? kSpacesOption : kTabsOption;

		if (SettingsLayout::Choice("##indentWith", kIndentOptions, selected))
		{
			m_Settings.SetIndentWithSpaces(selected == kSpacesOption);
		}
	}

	void EditorPage::DrawTabWidth()
	{
		int typedWidth = static_cast<int>(m_Settings.GetIndentStyle().Width);

		if (ImGui::InputInt("##tabWidth", &typedWidth))
		{
			constexpr int kMinimumTabWidth = static_cast<int>(EditorSettings::kMinimumIndentWidth);
			constexpr int kMaximumTabWidth = static_cast<int>(EditorSettings::kMaximumIndentWidth);

			m_Settings.SetIndentWidth(static_cast<std::size_t>(std::clamp(typedWidth, kMinimumTabWidth, kMaximumTabWidth)));
		}
	}

	void EditorPage::DrawNewFileLineEnding()
	{
		std::size_t selected = m_Settings.GetNewFileLineEnding() == Core::LineEnding::Crlf ? kCrlfOption : kLfOption;

		if (SettingsLayout::Choice("##newFileLineEnding", kLineEndingOptions, selected))
		{
			m_Settings.SetNewFileLineEnding(selected == kCrlfOption ? Core::LineEnding::Crlf : Core::LineEnding::Lf);
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
		int delay = m_HoverDelayWhileDragged.value_or(m_Settings.GetHoverSettings().DelayMs);

		if (ImGui::SliderInt("##hoverDelay", &delay, EditorSettings::kMinimumHoverDelayMs, EditorSettings::kMaximumHoverDelayMs, "%d ms"))
		{
			m_HoverDelayWhileDragged = delay;
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_HoverDelayWhileDragged.has_value())
		{
			m_Settings.SetHoverDelayMs(*m_HoverDelayWhileDragged);
			m_HoverDelayWhileDragged.reset();
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

	void EditorPage::DrawLineAuthor()
	{
		bool show = m_Settings.HasLineAuthor();

		if (Core::Toggle("##lineAuthor", &show))
		{
			m_Settings.SetLineAuthor(show);
		}
	}

	// The slider moves in steps of a tenth of a second, and the value is set once the drag ends, as the hover delay's is.
	void EditorPage::DrawLineAuthorDelay()
	{
		int delay = m_LineAuthorDelayWhileDragged.value_or(m_Settings.GetLineAuthorDelayMs());

		ImGui::BeginDisabled(!m_Settings.HasLineAuthor());

		if (ImGui::SliderInt("##lineAuthorDelay", &delay, EditorSettings::kMinimumLineAuthorDelayMs, EditorSettings::kMaximumLineAuthorDelayMs, "%d ms"))
		{
			m_LineAuthorDelayWhileDragged = ToNearestStep(delay, kLineAuthorDelayStepMs);
		}

		if (ImGui::IsItemDeactivatedAfterEdit() && m_LineAuthorDelayWhileDragged.has_value())
		{
			m_Settings.SetLineAuthorDelayMs(*m_LineAuthorDelayWhileDragged);
			m_LineAuthorDelayWhileDragged.reset();
		}

		ImGui::EndDisabled();
	}
}
