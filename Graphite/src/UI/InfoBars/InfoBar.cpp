#include "UI/InfoBars/InfoBar.h"

#include "UI/FittedText.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/TextMetrics.h"

#include <IconsLucide.h>
#include <imgui_internal.h>

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr float kWarningTint = 0.14f;
		constexpr float kWarningHoverTint = 0.30f;
		constexpr float kLongestSecondaryLabelInFontSizes = 18.0f;
		constexpr float kPaddingSides = 2.0f;
		constexpr bool kNotEngaged = false;

		// WARNING: DialogLayout's filled buttons use the same brightnesses, so a main action looks the same in a bar and
		// in a dialog; change both together.
		constexpr float kPrimaryHoveredBrightness = 1.15f;
		constexpr float kPrimaryActiveBrightness = 0.85f;
		constexpr int kPrimaryColourCount = 4;
		constexpr int kSecondaryColourCount = 1;
		const ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
		const ImVec4 kNoFill(0.0f, 0.0f, 0.0f, 0.0f);

		struct ShownButton
		{
			std::size_t Index = 0;
			std::string Label;
			bool Cut = false;
			float Width = 0.0f;
		};

		float ButtonWidthOf(const std::string& label)
		{
			return Core::TextWidth(label) + ImGui::GetStyle().FramePadding.x * kPaddingSides;
		}

		std::vector<ShownButton> FitButtons(const std::vector<InfoBarButton>& buttons, float room)
		{
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float sides = ImGui::GetStyle().FramePadding.x * kPaddingSides;
			const float longest = ImGui::GetFontSize() * kLongestSecondaryLabelInFontSizes;

			float left = room;

			for (const InfoBarButton& button : buttons)
			{
				if (button.Primary)
				{
					left -= ButtonWidthOf(button.Label) + spacing;
				}
			}

			std::vector<ShownButton> shown;

			for (std::size_t index = 0; index < buttons.size(); ++index)
			{
				const InfoBarButton& button = buttons[index];

				if (button.Primary)
				{
					shown.push_back({.Index = index, .Label = button.Label, .Width = ButtonWidthOf(button.Label)});
					continue;
				}

				const float labelRoom = std::min(longest, left - sides - spacing);
				ShownButton entry{.Index = index};
				entry.Label = labelRoom > 0.0f ? FitToWidth(button.Label, labelRoom, &entry.Cut) : std::string{};

				if (entry.Label.empty())
				{
					continue;
				}

				entry.Width = ButtonWidthOf(entry.Label);
				left -= entry.Width + spacing;
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

		// WARNING: The label is drawn here over a button of an id alone. As the button's own label, a path holding
		// "##", a folder named C# among them, would show only up to it.
		bool DrawButton(const ShownButton& button, const InfoBarButton& asked)
		{
			int pushed = kSecondaryColourCount;

			if (asked.Primary)
			{
				const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
				ImGui::PushStyleColor(ImGuiCol_Button, accent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Core::WithBrightnessScaled(accent, kPrimaryHoveredBrightness));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, Core::WithBrightnessScaled(accent, kPrimaryActiveBrightness));
				ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
				pushed = kPrimaryColourCount;
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button, kNoFill);
			}

			ImGui::PushID(static_cast<int>(button.Index));
			const bool pressed = Core::TextButton("###button", ImVec2(button.Width, 0.0f));
			ImGui::PopID();

			const ImGuiStyle& style = ImGui::GetStyle();
			const ImRect frame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			const ImVec2 textMin(frame.Min.x + style.FramePadding.x, frame.Min.y + style.FramePadding.y);
			const ImVec2 textMax(frame.Max.x - style.FramePadding.x, frame.Max.y - style.FramePadding.y);
			ImGui::RenderTextClippedEx(ImGui::GetWindowDrawList(), textMin, textMax, button.Label.data(),
									   button.Label.data() + button.Label.size(), nullptr, style.ButtonTextAlign, &frame);

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
		const ImVec4 surface = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);

		if (weight == InfoBarWeight::Warning)
		{
			const ImVec4 opaqueWeight(weightColour.x, weightColour.y, weightColour.z, surface.w);
			return Core::Animation::Blend(surface, opaqueWeight, kWarningTint);
		}

		return surface;
	}

	InfoBarAnswer DrawInfoBar(const InfoBarContent& content)
	{
		InfoBarAnswer answer;

		const ImGuiStyle& style = ImGui::GetStyle();
		const float spacing = style.ItemSpacing.x;
		const float barHeight = ImGui::GetFrameHeight() + style.FramePadding.y * kPaddingSides;
		const ImVec4 background = InfoBarBackground(content.Weight, content.WeightColour);

		ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(spacing, style.FramePadding.y));

		const bool shown = ImGui::BeginChild(content.Id, ImVec2(0.0f, barHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
											 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// WARNING: The hover colour is the surface's own colour lifted, which on a tinted warning bar is no lift at all;
		// the bar's buttons are hovered in its weight's colour instead.
		const bool tinted = content.Weight == InfoBarWeight::Warning;

		if (tinted)
		{
			const ImVec4 opaqueWeight(content.WeightColour.x, content.WeightColour.y, content.WeightColour.z, background.w);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Core::Animation::Blend(background, opaqueWeight, kWarningHoverTint));
		}

		if (shown)
		{
			const bool closable = content.CloseTooltip != nullptr;
			const float closeSize = Core::IconButtonSize();
			const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
			const float closePart = closable ? closeSize + spacing : 0.0f;

			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(content.WeightColour, "%s", content.Glyph);
			ImGui::SameLine();

			const float afterGlyph = ImGui::GetCursorPosX();
			const std::vector<ShownButton> buttons = FitButtons(content.Buttons, rowRight - closePart - afterGlyph);
			const float buttonsWidth = WidthOfAll(buttons);
			const float buttonsPart = buttons.empty() ? 0.0f : buttonsWidth + spacing;

			bool cut = false;
			const float messageRoom = std::max(0.0f, rowRight - closePart - buttonsPart - afterGlyph);
			const std::string message = FitToWidth(content.Message, messageRoom, &cut);

			if (message.empty())
			{
				ImGui::SetItemTooltip("%s", content.Message.c_str());
			}
			else
			{
				ImGui::TextUnformatted(message.c_str());

				if (cut)
				{
					ImGui::SetItemTooltip("%s", content.Message.c_str());
				}
			}

			float buttonX = std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + spacing,
									 rowRight - closePart - buttonsWidth);

			for (const ShownButton& button : buttons)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(buttonX);

				if (DrawButton(button, content.Buttons[button.Index]))
				{
					answer.Pressed = button.Index;
				}

				buttonX += button.Width + spacing;
			}

			if (closable)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(rowRight - closeSize);

				if (Core::DrawIconButton("##close", ICON_LC_X, kNotEngaged, content.CloseTooltip,
										 {.Flags = ImGuiButtonFlags_EnableNav}))
				{
					answer.Closed = true;
				}
			}
		}

		if (tinted)
		{
			ImGui::PopStyleColor();
		}

		ImGui::EndChild();

		return answer;
	}
}
