#pragma once

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"
#include "UI/FileIcons.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <string>

namespace Graphite {

	class DialogLayout final
	{
	public:
		DialogLayout() = delete;

		static constexpr float kNarrowWidth = 460.0f;
		static constexpr float kWideWidth = 600.0f;
		static constexpr float kPadding = 16.0f;
		static constexpr float kSectionGap = 6.0f;
		static constexpr float kButtonMinWidth = 90.0f;
		static constexpr std::size_t kListRows = 12;

		static void PushModalFrameColours()
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		}

		static void PopModalFrameColours()
		{
			ImGui::PopStyleColor();
		}

		static void BeforeBegin(float width)
		{
			const float scaledWidth = Core::Scaled(width);
			ImGui::SetNextWindowSizeConstraints(ImVec2(scaledWidth, 0.0f), ImVec2(scaledWidth, FLT_MAX));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Core::Scaled(ImVec2(kPadding, kPadding)));
		}

		static void AfterBegin()
		{
			ImGui::PopStyleVar();
		}

		static void Gap()
		{
			ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kSectionGap)));
		}

		static void Text(const std::string& text)
		{
			constexpr float kWrapAtWindowEdge = 0.0f;

			ImGui::PushTextWrapPos(kWrapAtWindowEdge);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopTextWrapPos();
		}

		static void MutedText(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			Text(text);
			ImGui::PopStyleColor();
		}

		static void ErrorText(const std::string& text, const ImVec4& colour)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			Text(text);
			ImGui::PopStyleColor();
		}

		static constexpr float kHeaderScale = 0.85f;

		static bool BeginFileList(const char* id, std::size_t rows, std::size_t headers)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

			// WARNING: measured in the header's own font, not as FontSize * kHeaderScale: ImGui rounds a
			// pushed size, and a list a pixel short shows a scrollbar with nothing to scroll.
			ImGui::PushFont(nullptr, style.FontSizeBase * kHeaderScale);
			const float headerHeight = ImGui::GetTextLineHeightWithSpacing();
			ImGui::PopFont();

			const float contentHeight =
				static_cast<float>(rows) * rowHeight + static_cast<float>(headers) * headerHeight;
			const float maximumHeight = static_cast<float>(kListRows) * rowHeight;
			const float listHeight = std::clamp(contentHeight, rowHeight, maximumHeight) - style.ItemSpacing.y +
									 style.WindowPadding.y * 2.0f;

			return ImGui::BeginChild(id, ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders);
		}

		static void EndFileList()
		{
			ImGui::EndChild();
		}

		static void FileGroupHeader(const char* text)
		{
			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHeaderScale);
			ImGui::TextDisabled("%s", text);
			ImGui::PopFont();
		}

		static void FileRow(FileKind kind, const std::string& path, const std::string& tag)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float width = ImGui::GetContentRegionAvail().x;
			const float height = ImGui::GetTextLineHeight();
			const ImVec2 start = ImGui::GetCursorScreenPos();

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 mutedColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

			DrawFileIcon(kind, start);

			const float pathX = start.x + IconToNameOffset();
			const float tagWidth = tag.empty() ? 0.0f : ImGui::CalcTextSize(tag.c_str()).x;
			const float tagX = start.x + width - tagWidth;
			const float pathEnd = tag.empty() ? start.x + width : tagX - style.ItemSpacing.x * 2.0f;

			const ImVec2 pathSize = ImGui::CalcTextSize(path.c_str());
			const bool isPathCut = pathX + pathSize.x > pathEnd;

			ImGui::RenderTextEllipsis(drawList, ImVec2(pathX, start.y), ImVec2(pathEnd, start.y + height), pathEnd,
									  path.c_str(), nullptr, &pathSize);

			if (!tag.empty())
			{
				drawList->AddText(ImVec2(tagX, start.y), mutedColour, tag.c_str());
			}

			ImGui::Dummy(ImVec2(width, height));

			if (isPathCut)
			{
				ImGui::SetItemTooltip("%s", path.c_str());
			}
		}

		struct ButtonSpec
		{
			const char* Label = nullptr;
			bool Enabled = true;
		};

		enum class Choice
		{
			None,
			Cancel,
			Safe,
			Destructive,
			Alternative
		};

		static bool FilledButton(const char* label, const ImVec4& fill, float width, bool enabled)
		{
			constexpr int kFilledButtonColourCount = 4;
			constexpr ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
			constexpr float kHoveredBrightness = 1.15f;
			constexpr float kActiveBrightness = 0.85f;

			ImGui::BeginDisabled(!enabled);

			if (enabled)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, fill);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Core::WithBrightnessScaled(fill, kHoveredBrightness));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, Core::WithBrightnessScaled(fill, kActiveBrightness));
				ImGui::PushStyleColor(ImGuiCol_Text, kWhite);
			}

			const bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));

			if (enabled)
			{
				ImGui::PopStyleColor(kFilledButtonColourCount);
			}

			ImGui::EndDisabled();
			return pressed;
		}

		static Choice ButtonRow(ButtonSpec safe, ButtonSpec destructive, const ImVec4& danger,
								ButtonSpec alternative = {}, const char* cancelLabel = "Cancel")
		{
			constexpr bool kHideTextAfterDoubleHash = true;
			constexpr bool kKeyRepeats = false;

			const ImGuiStyle& style = ImGui::GetStyle();

			float labelWidth = ImGui::CalcTextSize(cancelLabel, nullptr, kHideTextAfterDoubleHash).x;
			int buttonCount = 1;

			for (const ButtonSpec& spec : {safe, destructive, alternative})
			{
				if (spec.Label != nullptr)
				{
					labelWidth = std::max(labelWidth, ImGui::CalcTextSize(spec.Label, nullptr, kHideTextAfterDoubleHash).x);
					++buttonCount;
				}
			}

			const float buttonWidth = std::max(Core::Scaled(kButtonMinWidth), labelWidth + style.FramePadding.x * 2.0f);
			const float rowWidth = buttonWidth * static_cast<float>(buttonCount) +
								   style.ItemSpacing.x * static_cast<float>(buttonCount - 1);

			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - rowWidth, 0.0f));

			Choice clicked = Choice::None;

			const bool safeOffered = safe.Label != nullptr && safe.Enabled;

			if (ImGui::Button(cancelLabel, ImVec2(buttonWidth, 0.0f)))
			{
				clicked = Choice::Cancel;
			}

			if (!safeOffered)
			{
				ImGui::SetItemDefaultFocus();
			}

			if (alternative.Label != nullptr)
			{
				ImGui::SameLine();

				ImGui::BeginDisabled(!alternative.Enabled);

				if (ImGui::Button(alternative.Label, ImVec2(buttonWidth, 0.0f)))
				{
					clicked = Choice::Alternative;
				}

				ImGui::EndDisabled();
			}

			ImGuiID safeId = 0;

			if (safe.Label != nullptr)
			{
				ImGui::SameLine();

				const ImVec4 accentColour = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);

				if (FilledButton(safe.Label, accentColour, buttonWidth, safe.Enabled))
				{
					clicked = Choice::Safe;
				}

				safeId = ImGui::GetItemID();

				if (safeOffered)
				{
					ImGui::SetItemDefaultFocus();
				}
			}

			if (destructive.Label != nullptr)
			{
				ImGui::SameLine();

				if (FilledButton(destructive.Label, danger, buttonWidth, destructive.Enabled))
				{
					clicked = Choice::Destructive;
				}
			}

			if (clicked != Choice::None)
			{
				return clicked;
			}

			const bool dialogHasKeyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
										   !ImGui::IsWindowAppearing();

			if (!dialogHasKeyboard)
			{
				return Choice::None;
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Escape, kKeyRepeats))
			{
				return Choice::Cancel;
			}

			const bool enterPressed =
				ImGui::IsKeyPressed(ImGuiKey_Enter, kKeyRepeats) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, kKeyRepeats);

			if (!enterPressed || !safeOffered)
			{
				return Choice::None;
			}

			const ImGuiContext& context = *ImGui::GetCurrentContext();
			const bool isTypingInField = context.NavId != 0 && (context.ActiveId == context.NavId ||
																context.ActiveIdPreviousFrame == context.NavId);

			if (context.NavCursorVisible && context.NavId != safeId && !isTypingInField)
			{
				return Choice::None;
			}

			return Choice::Safe;
		}

		static Choice ButtonRow(ButtonSpec safe)
		{
			const ButtonSpec absentDestructive{};
			const ImVec4 dangerNeverDrawn{};

			return ButtonRow(safe, absentDestructive, dangerNeverDrawn);
		}

		static bool AcknowledgeRow(const char* label)
		{
			constexpr bool kKeyRepeats = false;

			const ButtonSpec absentButton{};
			const ImVec4 dangerNeverDrawn{};

			if (ButtonRow(absentButton, absentButton, dangerNeverDrawn, absentButton, label) == Choice::Cancel)
			{
				return true;
			}

			const bool dialogHasKeyboard = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
										   !ImGui::IsWindowAppearing();
			const bool enterPressed =
				ImGui::IsKeyPressed(ImGuiKey_Enter, kKeyRepeats) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, kKeyRepeats);

			return dialogHasKeyboard && enterPressed;
		}
	};
}
