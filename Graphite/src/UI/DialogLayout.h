#pragma once

#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <string>

// The shape every modal dialog of the Git panels shares: the padding, the gaps between
// sections, the file list, and the button row with its roles and keys. Kept in one place so
// the dialogs look like one family, and header-only so it needs nothing but ImGui and Core.
namespace Graphite::DialogLayout {

	// At 100%; each goes through Core::Scaled where it is used.
	inline constexpr float kNarrowWidth = 460.0f;
	inline constexpr float kWideWidth = 600.0f;
	inline constexpr float kPadding = 16.0f;
	inline constexpr float kSectionGap = 6.0f;
	inline constexpr float kButtonMinWidth = 90.0f;
	inline constexpr std::size_t kListRows = 12;

	// The theme gives frames and popups the same surface colour, so a checkbox or a field in
	// a modal had no edge at all. In a modal they take the window background instead: a well
	// sunk into the popup, the same contrast a field in a panel has the other way round.
	// ModalQuestion pushes this for every question it asks.
	inline void PushModalFrameColours()
	{
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	}

	inline void PopModalFrameColours() { ImGui::PopStyleColor(); }

	// Around the question's Begin. The padding is read by Begin and must not reach the
	// children drawn inside, so it is popped straight after; the width is fixed so the dialog
	// does not jump with the length of what it names.
	inline void BeforeBegin(float width)
	{
		const float scaled = Core::Scaled(width);
		ImGui::SetNextWindowSizeConstraints(ImVec2(scaled, 0.0f), ImVec2(scaled, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Core::Scaled(ImVec2(kPadding, kPadding)));
	}

	inline void AfterBegin() { ImGui::PopStyleVar(); }

	// Air between two sections: the text, the list, the options, the buttons.
	inline void Gap() { ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kSectionGap))); }

	// Wrapped at the dialog's edge, which the fixed width keeps where it is.
	inline void Text(const std::string& text)
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(text.c_str());
		ImGui::PopTextWrapPos();
	}

	inline void MutedText(const std::string& text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		Text(text);
		ImGui::PopStyleColor();
	}

	// Drawn only when there is something to say: the dialog grows by a line then, rather
	// than carrying an empty one the rest of the time.
	inline void ErrorText(const std::string& text, const ImVec4& colour)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		Text(text);
		ImGui::PopStyleColor();
	}

	// The group headers' size against the rows'.
	inline constexpr float kHeaderScale = 0.85f;

	// A bordered, rounded list sized to its `rows` and its group `headers` - each measured at
	// its own height - and scrolling past kListRows rows' worth rather than pushing the
	// buttons off screen.
	inline bool BeginFileList(const char* id, std::size_t rows, std::size_t headers)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
		const float headerHeight = ImGui::GetFontSize() * kHeaderScale + style.ItemSpacing.y;

		const float content = static_cast<float>(rows) * rowHeight + static_cast<float>(headers) * headerHeight;
		const float cap = static_cast<float>(kListRows) * rowHeight;
		const float height =
			std::clamp(content, rowHeight, cap) - style.ItemSpacing.y + style.WindowPadding.y * 2.0f;

		return ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	}

	inline void EndFileList() { ImGui::EndChild(); }

	// "CHANGED", "UNTRACKED": small and muted, so the paths are what the eye lands on.
	inline void FileGroupHeader(const char* text)
	{
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHeaderScale);
		ImGui::TextDisabled("%s", text);
		ImGui::PopFont();
	}

	// One row: the icon, the path - cut short with an ellipsis, whole in a tooltip - and,
	// right-aligned and muted, what makes the row matter. `tag` may be empty.
	inline void FileRow(const char* icon, const std::string& path, const std::string& tag)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float width = ImGui::GetContentRegionAvail().x;
		const float height = ImGui::GetTextLineHeight();
		const ImVec2 start = ImGui::GetCursorScreenPos();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		draw->AddText(start, muted, icon);

		const float pathX = start.x + ImGui::CalcTextSize(icon).x + style.ItemInnerSpacing.x * 2.0f;
		const float tagWidth = tag.empty() ? 0.0f : ImGui::CalcTextSize(tag.c_str()).x;
		const float tagX = start.x + width - tagWidth;
		const float pathEnd = tag.empty() ? start.x + width : tagX - style.ItemSpacing.x * 2.0f;

		const ImVec2 pathSize = ImGui::CalcTextSize(path.c_str());
		const bool cut = pathX + pathSize.x > pathEnd;

		ImGui::RenderTextEllipsis(draw, ImVec2(pathX, start.y), ImVec2(pathEnd, start.y + height), pathEnd,
								  path.c_str(), nullptr, &pathSize);

		if (!tag.empty())
		{
			draw->AddText(ImVec2(tagX, start.y), muted, tag.c_str());
		}

		// The row's item, which the window needs to end on and the tooltip hangs from.
		ImGui::Dummy(ImVec2(width, height));

		if (cut)
		{
			ImGui::SetItemTooltip("%s", path.c_str());
		}
	}

	// The two filled roles beside Cancel. A role with no label is left out.
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
		Destructive
	};

	namespace Detail {

		inline ImVec4 Brightened(const ImVec4& colour, float factor)
		{
			return ImVec4(std::min(colour.x * factor, 1.0f), std::min(colour.y * factor, 1.0f),
						  std::min(colour.z * factor, 1.0f), colour.w);
		}

		inline bool FilledButton(const char* label, const ImVec4& fill, float width, bool enabled)
		{
			ImGui::BeginDisabled(!enabled);

			// Filled only while it can be pressed: a disabled button in the accent or the
			// danger colour still reads as the thing to do.
			if (enabled)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, fill);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Brightened(fill, 1.15f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, Brightened(fill, 0.85f));

				// White whatever the scheme's text colour: a light scheme's dark text on a
				// strong fill is the one pairing that does not read.
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			}

			const bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));

			if (enabled)
			{
				ImGui::PopStyleColor(4);
			}

			ImGui::EndDisabled();
			return pressed;
		}
	}

	// The dialog's last row: right-aligned, every button as wide as the widest, in the order
	// Cancel, the safe action in the accent colour, the destructive one in `danger`. Escape is
	// Cancel; Enter is the safe action, never the destructive one, and nothing at all in a
	// dialog without a safe action. A click wins over a key pressed in the same frame.
	inline Choice ButtonRow(ButtonSpec safe, ButtonSpec destructive, const ImVec4& danger)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const char* cancelLabel = "Cancel";

		float labelWidth = ImGui::CalcTextSize(cancelLabel).x;
		int count = 1;

		for (const ButtonSpec& spec : {safe, destructive})
		{
			if (spec.Label != nullptr)
			{
				labelWidth = std::max(labelWidth, ImGui::CalcTextSize(spec.Label, nullptr, true).x);
				++count;
			}
		}

		const float buttonWidth = std::max(Core::Scaled(kButtonMinWidth), labelWidth + style.FramePadding.x * 2.0f);
		const float rowWidth =
			buttonWidth * static_cast<float>(count) + style.ItemSpacing.x * static_cast<float>(count - 1);

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(ImGui::GetContentRegionAvail().x - rowWidth, 0.0f));

		Choice clicked = Choice::None;

		if (ImGui::Button(cancelLabel, ImVec2(buttonWidth, 0.0f)))
		{
			clicked = Choice::Cancel;
		}

		if (safe.Label != nullptr)
		{
			ImGui::SameLine();

			// The theme's accent, which it also gives the check mark.
			if (Detail::FilledButton(safe.Label, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), buttonWidth,
									 safe.Enabled))
			{
				clicked = Choice::Safe;
			}
		}

		if (destructive.Label != nullptr)
		{
			ImGui::SameLine();

			if (Detail::FilledButton(destructive.Label, danger, buttonWidth, destructive.Enabled))
			{
				clicked = Choice::Destructive;
			}
		}

		if (clicked != Choice::None)
		{
			return clicked;
		}

		// Only while this dialog has the keyboard, and not on the frame it appears: the Enter
		// that opened it from a list would otherwise answer it at once.
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsWindowAppearing())
		{
			return Choice::None;
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			return Choice::Cancel;
		}

		const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);

		if (enter && safe.Label != nullptr && safe.Enabled)
		{
			return Choice::Safe;
		}

		return Choice::None;
	}
}
