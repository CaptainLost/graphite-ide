#include "Settings/Panel/SettingsLayout.h"

#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/Toggle.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kCardPadding = 14.0f;
		constexpr float kRowPadding = 10.0f;
		constexpr float kTextControlGap = 16.0f;
		constexpr float kStackBelowCardWidth = 420.0f;
		constexpr float kCardRounding = 6.0f;
		constexpr float kCardGap = 18.0f;
		constexpr float kPageTitleGap = 10.0f;
		constexpr float kResultsLabelGap = 5.0f;
		constexpr float kPageTitleScale = 1.4f;
		constexpr float kResultsLabelScale = 1.15f;
		constexpr float kSliderWidth = 200.0f;
		constexpr float kNumberWidth = 120.0f;
		constexpr float kMinimumComboWidth = 140.0f;
		constexpr float kNavigationEntryHeight = 28.0f;
		constexpr float kNavigationEntryInset = 8.0f;
		constexpr float kNavigationEntryRounding = 5.0f;
		constexpr float kIconSlotFontSizes = 1.6f;
		constexpr float kGroupGap = 10.0f;
		constexpr const char* kSearchHint = "Search settings\xE2\x80\xA6";

		constexpr int kCardLayerCount = 2;
		constexpr int kBackgroundLayer = 0;
		constexpr int kForegroundLayer = 1;
		constexpr int kCardColourCount = 1;

		// WARNING: The colours and their order are a list row's, so a selected entry keeps the accent under the
		// pointer. The hover fades in and out, from the colour the entry rests in to the one it takes under the
		// pointer; a press comes only after the pointer has rested there, so it shows at once.
		ImVec4 NavigationEntryFill(bool selected, bool held, float hover)
		{
			constexpr bool kUnderThePointer = true;
			constexpr bool kAway = false;
			constexpr bool kNotHeld = false;
			constexpr float kClearAlpha = 0.0f;

			const ImVec4 underThePointer = ListRowHighlight(selected, kUnderThePointer, held).value();
			const ImVec4 resting = ListRowHighlight(selected, kAway, kNotHeld)
									   .value_or(Core::WithAlphaReplaced(underThePointer, kClearAlpha));

			return Core::Animation::Blend(resting, underThePointer, hover);
		}

		CardMeasures MeasuresFor(float cardWidth)
		{
			return CardMeasures{.CardWidth = cardWidth,
								.Padding = Core::Scaled(kCardPadding),
								.Gap = Core::Scaled(kTextControlGap),
								.StackBelowWidth = Core::Scaled(kStackBelowCardWidth)};
		}

		float CardWidthHere()
		{
			return ImGui::GetContentRegionAvail().x;
		}

		// WARNING: A field's fill is see-through, so on a card it lifts the card's colour by the step it lifts the chrome,
		// and its border and the hover surface stand out from the card as they do from the chrome. Only a child window
		// inside a row is cleared, so it shows the card rather than painting the window colour over it.
		void PushCardColours()
		{
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		}

		void PopCardColours()
		{
			ImGui::PopStyleColor(kCardColourCount);
		}

		float TextHeight(const std::string& text, float wrapWidth)
		{
			if (text.empty())
			{
				return 0.0f;
			}

			const bool hideTextAfterDoubleHash = false;

			return ImGui::CalcTextSize(text.c_str(), nullptr, hideTextAfterDoubleHash, wrapWidth).y;
		}

		float MeasureRowText(const SettingsRow& row, float wrapWidth)
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.y;
			float height = TextHeight(row.Title, wrapWidth);

			if (!row.Description.empty())
			{
				height += spacing + TextHeight(row.Description, wrapWidth);
			}

			if (!row.Problem.empty())
			{
				height += spacing + TextHeight(row.Problem, wrapWidth);
			}

			return height;
		}

		void TextInColour(const std::string& text, ImU32 colour)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
		}

		// WARNING: The group makes every line after the first start where the first did, not at the window's edge.
		void DrawRowText(const SettingsRow& row, float wrapRightScreenX, ImU32 problemColour)
		{
			ImGui::BeginGroup();
			ImGui::PushTextWrapPos(wrapRightScreenX - ImGui::GetWindowPos().x + ImGui::GetScrollX());

			if (row.DimmedTitle)
			{
				TextInColour(row.Title, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			}
			else
			{
				ImGui::TextUnformatted(row.Title.c_str());
			}

			if (!row.Description.empty())
			{
				TextInColour(row.Description, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			}

			if (!row.Problem.empty())
			{
				TextInColour(row.Problem, problemColour);
			}

			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
		}

		bool HasControl(const SettingsRow& row)
		{
			return row.DrawControl && (row.WideControl || row.ControlWidth > 0.0f);
		}

		void DrawControlRow(const SettingsRow& row, float cardWidth, ImU32 problemColour)
		{
			const CardMeasures measures = MeasuresFor(cardWidth);
			const bool hasControl = HasControl(row);
			const RowColumns columns = LayOutRowColumns(measures, hasControl ? row.ControlWidth : 0.0f, hasControl && row.WideControl);
			const RowContent content{.TextHeight = MeasureRowText(row, columns.TextWidth),
									 .ControlHeight = hasControl ? ImGui::GetFrameHeight() : 0.0f,
									 .Padding = Core::Scaled(kRowPadding),
									 .Spacing = ImGui::GetStyle().ItemSpacing.y};
			const RowHeights heights = MeasureRowHeights(columns, content);

			const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();
			const float insideLeft = rowTopLeft.x + measures.Padding;

			ImGui::SetCursorScreenPos(ImVec2(insideLeft, rowTopLeft.y + heights.TextTop));
			DrawRowText(row, insideLeft + columns.TextWidth, problemColour);

			if (hasControl)
			{
				ImGui::SetCursorScreenPos(ImVec2(insideLeft + columns.ControlX, rowTopLeft.y + heights.ControlTop));
				ImGui::PushItemWidth(columns.ControlWidth);
				ImGui::BeginGroup();
				row.DrawControl();
				ImGui::EndGroup();
				ImGui::PopItemWidth();
			}

			ImGui::SetCursorScreenPos(rowTopLeft);
			ImGui::Dummy(ImVec2(cardWidth, heights.Row));
		}

		void DrawBlockRow(const SettingsRow& row, float cardWidth)
		{
			const float padding = Core::Scaled(kCardPadding);
			const float rowPadding = Core::Scaled(kRowPadding);
			const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();

			ImGui::SetCursorScreenPos(ImVec2(rowTopLeft.x + padding, rowTopLeft.y + rowPadding));

			ImGuiChildFlags blockFlags = ImGuiChildFlags_AutoResizeY;

			if (row.DrawnEvenOutOfSight)
			{
				blockFlags |= ImGuiChildFlags_AlwaysAutoResize;
			}

			if (ImGui::BeginChild("##block", ImVec2(cardWidth - 2.0f * padding, 0.0f), blockFlags))
			{
				row.DrawBlock();
			}

			ImGui::EndChild();

			const float blockBottom = ImGui::GetItemRectMax().y;
			ImGui::SetCursorScreenPos(rowTopLeft);
			ImGui::Dummy(ImVec2(cardWidth, blockBottom + rowPadding - rowTopLeft.y));
		}

		void DrawRow(const SettingsRow& row, float cardWidth, ImU32 problemColour)
		{
			const std::string_view key = row.GetKey();
			ImGui::PushID(key.data(), key.data() + key.size());

			if (row.IsBlock())
			{
				DrawBlockRow(row, cardWidth);
			}
			else
			{
				DrawControlRow(row, cardWidth, problemColour);
			}

			ImGui::PopID();
		}

		void DrawCardHeading(const SettingsCard& card, float cardWidth)
		{
			if (!card.Heading.empty())
			{
				ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), 0.0f);
				ImGui::TextUnformatted(card.Heading.c_str());
				ImGui::PopFont();
			}

			if (!card.Note.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cardWidth);
				TextInColour(card.Note, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::PopTextWrapPos();
			}
		}

		// WARNING: PushFont takes the size before the interface scale is applied, so build it from
		// FontSizeBase, never from GetFontSize, which already carries the scale.
		void DrawScaledBoldText(std::string_view text, float scale)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), ImGui::GetStyle().FontSizeBase * scale);
			ImGui::TextUnformatted(text.data(), text.data() + text.size());
			ImGui::PopFont();
		}
	}

	RowColumns LayOutRowColumns(const CardMeasures& card, float controlWidth, bool wideControl)
	{
		const float insideWidth = std::max(card.CardWidth - 2.0f * card.Padding, 0.0f);
		const bool hasControl = wideControl || controlWidth > 0.0f;

		if (!hasControl)
		{
			return RowColumns{.TextWidth = insideWidth};
		}

		if (card.CardWidth < card.StackBelowWidth)
		{
			const float stackedWidth = wideControl ? insideWidth : std::min(controlWidth, insideWidth);
			return RowColumns{.Stacked = true, .TextWidth = insideWidth, .ControlX = 0.0f, .ControlWidth = stackedWidth};
		}

		const float halfWidth = insideWidth * 0.5f;
		const float sideWidth = wideControl ? halfWidth : std::min(controlWidth, halfWidth);

		return RowColumns{.TextWidth = insideWidth - sideWidth - card.Gap, .ControlX = insideWidth - sideWidth, .ControlWidth = sideWidth};
	}

	RowHeights MeasureRowHeights(const RowColumns& columns, const RowContent& content)
	{
		if (columns.Stacked)
		{
			const float controlTop = content.Padding + content.TextHeight + content.Spacing;
			return RowHeights{.Row = controlTop + content.ControlHeight + content.Padding, .TextTop = content.Padding, .ControlTop = controlTop};
		}

		const float tallest = std::max(content.TextHeight, content.ControlHeight);

		return RowHeights{.Row = tallest + 2.0f * content.Padding,
						  .TextTop = content.Padding + (tallest - content.TextHeight) * 0.5f,
						  .ControlTop = content.Padding + (tallest - content.ControlHeight) * 0.5f};
	}

	float SettingsLayout::ToggleWidth()
	{
		return Core::Scaled(Core::kToggleWidth);
	}

	float SettingsLayout::SliderWidth()
	{
		return Core::Scaled(kSliderWidth);
	}

	float SettingsLayout::NumberWidth()
	{
		return Core::Scaled(kNumberWidth);
	}

	float SettingsLayout::ComboWidth(std::span<const std::string> options)
	{
		float widestOption = 0.0f;

		for (const std::string& option : options)
		{
			widestOption = std::max(widestOption, ImGui::CalcTextSize(option.c_str()).x);
		}

		const float width = widestOption + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();

		return std::max(width, Core::Scaled(kMinimumComboWidth));
	}

	float SettingsLayout::ButtonsWidth(std::span<const std::string> labels)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		float width = 0.0f;

		for (const std::string& label : labels)
		{
			width += ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
		}

		if (!labels.empty())
		{
			width += style.ItemSpacing.x * static_cast<float>(labels.size() - 1);
		}

		return width;
	}

	void SettingsLayout::DrawPageTitle(std::string_view title)
	{
		DrawScaledBoldText(title, kPageTitleScale);
		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kPageTitleGap)));
	}

	void SettingsLayout::DrawResultsLabel(std::string_view label)
	{
		DrawScaledBoldText(label, kResultsLabelScale);
		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kResultsLabelGap)));
	}

	void SettingsLayout::DrawCard(const SettingsCard& card, ImU32 problemColour)
	{
		const float cardWidth = CardWidthHere();
		DrawCardHeading(card, cardWidth);

		if (card.Rows.empty())
		{
			return;
		}

		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const float padding = Core::Scaled(kCardPadding);
		const ImU32 surface = ImGui::GetColorU32(Core::SurfaceColour());
		const ImU32 divider = ImGui::GetColorU32(ImGuiCol_Separator);
		const ImVec2 cardTopLeft = ImGui::GetCursorScreenPos();

		// WARNING: This is a splitter of its own, not the draw list's: a table inside a row splits the
		// draw list too, and the draw list's own splitter cannot be split twice.
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImDrawListSplitter layers;
		layers.Split(drawList, kCardLayerCount);
		layers.SetCurrentChannel(drawList, kForegroundLayer);
		PushCardColours();

		for (std::size_t rowIndex = 0; rowIndex < card.Rows.size(); ++rowIndex)
		{
			if (rowIndex > 0)
			{
				const float dividerY = ImGui::GetCursorScreenPos().y - spacing * 0.5f;
				drawList->AddLine(ImVec2(cardTopLeft.x + padding, dividerY), ImVec2(cardTopLeft.x + cardWidth - padding, dividerY), divider);
			}

			DrawRow(card.Rows[rowIndex], cardWidth, problemColour);
		}

		PopCardColours();

		const float cardBottom = ImGui::GetCursorScreenPos().y - spacing;
		layers.SetCurrentChannel(drawList, kBackgroundLayer);
		drawList->AddRectFilled(cardTopLeft, ImVec2(cardTopLeft.x + cardWidth, cardBottom), surface, Core::Scaled(kCardRounding));
		layers.Merge(drawList);

		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kCardGap)));
	}

	void SettingsLayout::DrawPageBody(SettingsPage& page, ImU32 problemColour)
	{
		DrawPageTitle(page.GetTitle());

		const std::vector<SettingsCard> cards = page.Describe();

		for (std::size_t cardIndex = 0; cardIndex < cards.size(); ++cardIndex)
		{
			ImGui::PushID(static_cast<int>(cardIndex));
			DrawCard(cards[cardIndex], problemColour);
			ImGui::PopID();
		}
	}

	bool SettingsLayout::DrawSearchField(std::string& query)
	{
		return DrawFilterField("##searchSettings", query, {.Hint = kSearchHint});
	}

	void SettingsLayout::DrawGroupTitle(std::string_view title)
	{
		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kGroupGap)));
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Core::Scaled(kNavigationEntryInset));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextUnformatted(title.data(), title.data() + title.size());
		ImGui::PopStyleColor();
	}

	void SettingsLayout::DrawGroupDivider()
	{
		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kGroupGap) * 0.5f));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kGroupGap) * 0.5f));
	}

	bool SettingsLayout::DrawNavigationEntry(const char* icon, std::string_view title, bool selected, bool dimmed,
											 std::optional<FileKind> badge)
	{
		const ImVec2 topLeft = ImGui::GetCursorScreenPos();
		const ImVec2 size(ImGui::GetContentRegionAvail().x, Core::Scaled(kNavigationEntryHeight));

		ImGui::PushID(title.data(), title.data() + title.size());
		const bool clicked = ImGui::InvisibleButton("##entry", size, ImGuiButtonFlags_EnableNav);
		const float hover = Core::Animation::HoverAmount(ImGui::GetItemID(), ImGui::IsItemHovered());
		const bool held = ImGui::IsItemActive();
		ImGui::PopID();

		const ImVec4 background = NavigationEntryFill(selected, held, hover);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + size.x, topLeft.y + size.y), ImGui::GetColorU32(background),
								Core::Scaled(kNavigationEntryRounding));

		const ImU32 textColour = ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
		const float textTop = topLeft.y + (size.y - ImGui::GetFontSize()) * 0.5f;
		const float iconLeft = topLeft.x + Core::Scaled(kNavigationEntryInset);
		const float titleLeft = iconLeft + ImGui::GetFontSize() * kIconSlotFontSizes;

		if (badge.has_value())
		{
			DrawFileIcon(*badge, ImVec2(iconLeft, textTop), dimmed ? IconTone::Dimmed : IconTone::Normal);
		}
		else
		{
			drawList->AddText(ImVec2(iconLeft, textTop), textColour, icon);
		}

		drawList->AddText(ImVec2(titleLeft, textTop), textColour, title.data(), title.data() + title.size());

		return clicked;
	}

	bool SettingsLayout::Choice(const char* id, std::span<const std::string> options, std::size_t& selected,
								const std::string& unlistedPreview)
	{
		const char* preview = selected < options.size() ? options[selected].c_str() : unlistedPreview.c_str();

		if (!ImGui::BeginCombo(id, preview))
		{
			return false;
		}

		bool changed = false;

		for (std::size_t optionIndex = 0; optionIndex < options.size(); ++optionIndex)
		{
			const bool isSelected = optionIndex == selected;

			if (ImGui::Selectable(options[optionIndex].c_str(), isSelected) && !isSelected)
			{
				selected = optionIndex;
				changed = true;
			}

			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();

		return changed;
	}
}
