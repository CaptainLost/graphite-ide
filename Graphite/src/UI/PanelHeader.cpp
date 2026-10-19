#include "UI/PanelHeader.h"

#include "UI/FittedText.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kDropdownHeight = 24.0f;
		constexpr float kDropdownPadding = 6.0f;
		constexpr float kPaddingsPerDropdown = 2.0f;
		constexpr float kNameToChevronGap = 5.0f;
		constexpr float kBetweenDropdowns = 2.0f;
		constexpr float kBeforeActions = 8.0f;
		constexpr float kBetweenActions = 1.0f;
		constexpr float kDropdownRounding = 5.0f;
		constexpr float kBelowDropdown = 2.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kInvisibleAlpha = 0.0f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kNoLimit = FLT_MAX;
		constexpr float kAnyHeight = 0.0f;
		constexpr bool kNotEngaged = false;
		constexpr const char* kDropdownId = "##dropdown";
		constexpr const char* kOptionsId = "##options";
		constexpr const char* kChevron = ICON_LC_CHEVRON_DOWN;
		constexpr Core::IconButtonOptions kAction{.Fit = Core::IconButtonFit::Box, .Rest = Core::IconButtonRest::Dim};

		using ChoiceWidths = std::array<float, kPanelHeaderMostChoices>;

		std::string_view CurrentName(const PanelHeaderChoice& choice)
		{
			const bool selectsAnOption =
				choice.Selected >= 0 && static_cast<std::size_t>(choice.Selected) < choice.Options.size();

			return selectsAnOption ? std::string_view{choice.Options[static_cast<std::size_t>(choice.Selected)]}
								   : std::string_view{};
		}

		float StrongWidth(std::string_view text)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const float width = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
			ImGui::PopFont();

			return width;
		}

		float LeastDropdownWidth()
		{
			return Core::Scaled(kDropdownPadding) * kPaddingsPerDropdown + ImGui::CalcTextSize(kChevron).x;
		}

		float NaturalDropdownWidth(const PanelHeaderChoice& choice)
		{
			return std::ceil(LeastDropdownWidth() + Core::Scaled(kNameToChevronGap) + StrongWidth(CurrentName(choice)));
		}

		// WARNING: The narrowest dropdown is given its whole width first, so a short name is never cut to make room
		// for a long one beside it.
		ChoiceWidths FitDropdowns(std::span<const PanelHeaderChoice> choices, float room)
		{
			ChoiceWidths natural{};
			std::array<std::size_t, kPanelHeaderMostChoices> narrowestFirst{};
			std::iota(narrowestFirst.begin(), narrowestFirst.end(), std::size_t{0});

			for (std::size_t index = 0; index < choices.size(); ++index)
			{
				natural[index] = NaturalDropdownWidth(choices[index]);
			}

			const auto shown = narrowestFirst.begin() + static_cast<std::ptrdiff_t>(choices.size());
			std::sort(narrowestFirst.begin(), shown,
					  [&natural](std::size_t left, std::size_t right) { return natural[left] < natural[right]; });

			ChoiceWidths widths{};
			float roomLeft = room;
			std::size_t stillToFit = choices.size();

			for (auto next = narrowestFirst.begin(); next != shown; ++next)
			{
				const float share = std::floor(roomLeft / static_cast<float>(stillToFit));
				widths[*next] = std::max(LeastDropdownWidth(), std::min(natural[*next], share));
				roomLeft -= widths[*next];
				--stillToFit;
			}

			return widths;
		}

		void DrawDropdownFace(const ImRect& rect, std::string_view name, float hoverAmount, bool held)
		{
			ImDrawList* const drawList = ImGui::GetWindowDrawList();

			const ImVec4 hoveredFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
			const ImVec4 fill = held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
									 : Core::Animation::Blend(Core::WithAlphaReplaced(hoveredFill, kInvisibleAlpha),
															  hoveredFill, hoverAmount);

			if (fill.w > 0.0f)
			{
				drawList->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(fill), Core::Scaled(kDropdownRounding));
			}

			const float padding = Core::Scaled(kDropdownPadding);
			const float chevronWidth = ImGui::CalcTextSize(kChevron).x;
			const float textTop = std::floor(rect.Min.y + (rect.GetHeight() - ImGui::GetFontSize()) * kHalf);
			const float nameRoom = rect.GetWidth() - LeastDropdownWidth() - Core::Scaled(kNameToChevronGap);

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			const std::string shownName = FitToWidth(name, nameRoom);
			drawList->AddText(ImVec2(rect.Min.x + padding, textTop), Core::EmphasisText(), shownName.c_str());
			ImGui::PopFont();

			const float chevronLeft = rect.Max.x - padding - chevronWidth;
			drawList->AddText(ImVec2(chevronLeft, textTop), ImGui::GetColorU32(ImGuiCol_TextDisabled), kChevron);
		}

		std::optional<int> DrawOptions(const PanelHeaderChoice& choice, const ImRect& dropdown)
		{
			if (!ImGui::IsPopupOpen(kOptionsId))
			{
				return std::nullopt;
			}

			ImGui::SetNextWindowPos(ImVec2(dropdown.Min.x, dropdown.Max.y + Core::Scaled(kBelowDropdown)));
			ImGui::SetNextWindowSizeConstraints(ImVec2(dropdown.GetWidth(), kAnyHeight), ImVec2(kNoLimit, kNoLimit));

			if (!ImGui::BeginPopup(kOptionsId))
			{
				return std::nullopt;
			}

			std::optional<int> chosen;

			for (std::size_t index = 0; index < choice.Options.size(); ++index)
			{
				const int option = static_cast<int>(index);

				ImGui::PushID(option);

				if (ImGui::Selectable(choice.Options[index].c_str(), option == choice.Selected))
				{
					chosen = option;
				}

				ImGui::PopID();
			}

			ImGui::EndPopup();
			return chosen;
		}

		std::optional<int> DrawDropdown(const PanelHeaderChoice& choice, const ImRect& rect)
		{
			ImGui::PushID(choice.Id);
			const ImGuiID id = ImGui::GetID(kDropdownId);

			if (ImGui::ItemAdd(rect, id))
			{
				bool hovered = false;
				bool held = false;

				if (ImGui::ButtonBehavior(rect, id, &hovered, &held))
				{
					ImGui::OpenPopup(kOptionsId);
				}

				// WARNING: Open counts as hovered, so the dropdown keeps its fill while its options are shown and the
				// pointer is over them, outside the header.
				const bool open = ImGui::IsPopupOpen(kOptionsId);
				DrawDropdownFace(rect, CurrentName(choice), Core::Animation::HoverAmount(id, hovered || open),
								 held && hovered);
			}

			const std::optional<int> chosen = DrawOptions(choice, rect);
			ImGui::PopID();

			return chosen;
		}

		float ActionsWidth(std::size_t count)
		{
			if (count == 0)
			{
				return 0.0f;
			}

			const float buttons = Core::IconButtonSize(kAction.Fit) * static_cast<float>(count);
			return buttons + Core::Scaled(kBetweenActions) * static_cast<float>(count - 1);
		}

		std::optional<std::size_t> DrawActions(std::span<const PanelHeaderAction> actions, float left, float rowTop,
											   float rowHeight)
		{
			const float buttonTop = rowTop + std::floor((rowHeight - Core::IconButtonSize(kAction.Fit)) * kHalf);
			std::optional<std::size_t> clicked;
			float x = left;

			for (std::size_t index = 0; index < actions.size(); ++index)
			{
				const PanelHeaderAction& action = actions[index];

				ImGui::SetCursorScreenPos(ImVec2(x, buttonTop));

				if (Core::DrawIconButton(action.Id, action.Glyph, kNotEngaged, action.Tooltip, kAction))
				{
					clicked = index;
				}

				x += Core::IconButtonSize(kAction.Fit) + Core::Scaled(kBetweenActions);
			}

			return clicked;
		}
	}

	float PanelHeaderHeight()
	{
		return Core::Scaled(kPanelHeaderHeight);
	}

	// WARNING: Every piece is placed by position inside the row and the row is then laid out once, by the Dummy at its
	// start. Whatever follows starts on the line under the whole row, and ImGui is never left with a cursor moved
	// past what was submitted.
	PanelHeaderResult DrawPanelHeader(std::span<const PanelHeaderChoice> choices,
									  std::span<const PanelHeaderAction> actions)
	{
		PanelHeaderResult result;

		if (ImGui::GetCurrentWindow()->SkipItems)
		{
			return result;
		}

		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float rowWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
		const float rowHeight = PanelHeaderHeight();
		const float actionsLeft = std::floor(rowStart.x + rowWidth - ActionsWidth(actions.size()));

		const std::span<const PanelHeaderChoice> shown = choices.first(std::min(choices.size(), kPanelHeaderMostChoices));
		const float choicesRight = actions.empty() ? rowStart.x + rowWidth : actionsLeft - Core::Scaled(kBeforeActions);
		const float gapsBetween =
			shown.empty() ? 0.0f : Core::Scaled(kBetweenDropdowns) * static_cast<float>(shown.size() - 1);
		const ChoiceWidths widths = FitDropdowns(shown, choicesRight - rowStart.x - gapsBetween);

		const float dropdownHeight = Core::Scaled(kDropdownHeight);
		const float dropdownTop = rowStart.y + std::floor((rowHeight - dropdownHeight) * kHalf);
		float dropdownLeft = rowStart.x;

		for (std::size_t index = 0; index < shown.size(); ++index)
		{
			const ImRect rect(ImVec2(dropdownLeft, dropdownTop),
							  ImVec2(dropdownLeft + widths[index], dropdownTop + dropdownHeight));

			result.Chosen[index] = DrawDropdown(shown[index], rect);
			dropdownLeft = rect.Max.x + Core::Scaled(kBetweenDropdowns);
		}

		result.ClickedAction = DrawActions(actions, actionsLeft, rowStart.y, rowHeight);

		ImGui::SetCursorScreenPos(rowStart);
		ImGui::Dummy(ImVec2(rowWidth, rowHeight));

		return result;
	}
}
