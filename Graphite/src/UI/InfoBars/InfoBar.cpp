#include "UI/InfoBars/InfoBar.h"

#include "UI/FittedText.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/IconButton.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr float kWarningTint = 0.14f;
		constexpr float kPrimaryHoverLightening = 0.12f;
		constexpr float kPrimaryPressDarkening = 0.12f;
		constexpr float kLongestSecondaryLabelInFontSizes = 18.0f;
		constexpr float kPaddingSides = 2.0f;
		constexpr bool kNotEngaged = false;
		constexpr int kPrimaryColourCount = 4;
		constexpr int kSecondaryColourCount = 1;
		const ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 kBlack(0.0f, 0.0f, 0.0f, 1.0f);
		const ImVec4 kNoFill(0.0f, 0.0f, 0.0f, 0.0f);

		struct ShownButton
		{
			std::string Label;
			bool Cut = false;
			float Width = 0.0f;
		};

		// WARNING: The id after "###" keeps a button's id fixed whatever its label holds, a path included.
		std::string ButtonId(const std::string& label)
		{
			return label + "###button";
		}

		std::vector<ShownButton> ShownButtons(const std::vector<InfoBarButton>& buttons)
		{
			const float longest = ImGui::GetFontSize() * kLongestSecondaryLabelInFontSizes;
			const float sides = ImGui::GetStyle().FramePadding.x * kPaddingSides;

			std::vector<ShownButton> shown;
			shown.reserve(buttons.size());

			for (const InfoBarButton& button : buttons)
			{
				ShownButton entry;
				entry.Label = button.Primary ? button.Label : FitToWidth(button.Label, longest, &entry.Cut);
				entry.Width = ImGui::CalcTextSize(ButtonId(entry.Label).c_str(), nullptr, true).x + sides;
				shown.push_back(std::move(entry));
			}

			return shown;
		}

		float WidthOfAll(const std::vector<ShownButton>& buttons)
		{
			float width = 0.0f;

			for (const ShownButton& button : buttons)
			{
				width += button.Width;
			}

			const float gaps = buttons.empty() ? 0.0f : static_cast<float>(buttons.size() - 1);
			return width + gaps * ImGui::GetStyle().ItemSpacing.x;
		}

		bool DrawButton(const ShownButton& button, const InfoBarButton& asked, std::size_t index)
		{
			int pushed = kSecondaryColourCount;

			if (asked.Primary)
			{
				const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
				ImGui::PushStyleColor(ImGuiCol_Button, accent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Core::Animation::Blend(accent, kWhite, kPrimaryHoverLightening));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, Core::Animation::Blend(accent, kBlack, kPrimaryPressDarkening));
				ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
				pushed = kPrimaryColourCount;
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, kNoFill);
			}

			ImGui::PushID(static_cast<int>(index));
			const bool pressed = ImGui::Button(ButtonId(button.Label).c_str());
			ImGui::PopID();
			ImGui::PopStyleColor(pushed);

			if (button.Cut)
			{
				ImGui::SetItemTooltip("%s", asked.Label.c_str());
			}

			return pressed;
		}
	}

	ImVec4 InfoBarBackground(InfoBarWeight weight, const ImVec4& weightColour)
	{
		const ImVec4 menuBar = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);

		if (weight == InfoBarWeight::Warning)
		{
			const ImVec4 opaqueWeight(weightColour.x, weightColour.y, weightColour.z, menuBar.w);
			return Core::Animation::Blend(menuBar, opaqueWeight, kWarningTint);
		}

		return menuBar;
	}

	InfoBarAnswer DrawInfoBar(const InfoBarContent& content)
	{
		InfoBarAnswer answer;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float spacing = style.ItemSpacing.x;
		const float barHeight = ImGui::GetFrameHeight() + style.FramePadding.y * kPaddingSides;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, InfoBarBackground(content.Weight, content.WeightColour));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(spacing, style.FramePadding.y));

		const bool shown = ImGui::BeginChild(content.Id, ImVec2(0.0f, barHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
											 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		if (shown)
		{
			const std::vector<ShownButton> buttons = ShownButtons(content.Buttons);
			const float buttonsWidth = WidthOfAll(buttons);
			const bool closable = content.CloseTooltip != nullptr;
			const float closeSize = Core::IconButtonSize();

			const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			const float closePart = closable ? closeSize + spacing : 0.0f;
			const float buttonsPart = buttons.empty() ? 0.0f : buttonsWidth + spacing;

			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(content.WeightColour, "%s", content.Glyph);
			ImGui::SameLine();

			bool cut = false;
			const float messageRoom = std::max(0.0f, rowRight - closePart - buttonsPart - ImGui::GetCursorPosX());
			const std::string message = FitToWidth(content.Message, messageRoom, &cut);
			ImGui::TextUnformatted(message.c_str());

			if (cut)
			{
				ImGui::SetItemTooltip("%s", content.Message.c_str());
			}

			// The buttons stand at the bar's end, before its close button, however short the message.
			float buttonX = std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + spacing,
									 rowRight - closePart - buttonsWidth);

			for (std::size_t index = 0; index < buttons.size(); ++index)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(buttonX);

				if (DrawButton(buttons[index], content.Buttons[index], index))
				{
					answer.Pressed = index;
				}

				buttonX += buttons[index].Width + spacing;
			}

			if (closable)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(rowRight - closeSize);

				if (Core::DrawIconButton("##close", ICON_MDI_CLOSE, kNotEngaged, content.CloseTooltip))
				{
					answer.Closed = true;
				}
			}
		}

		ImGui::EndChild();

		return answer;
	}
}
