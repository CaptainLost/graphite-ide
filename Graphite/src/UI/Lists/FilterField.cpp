#include "UI/Lists/FilterField.h"

#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/TextInput.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Graphite {

	namespace {
		constexpr float kHalf = 0.5f;
		constexpr bool kNotEngaged = false;
		constexpr float kRightAfter = 0.0f;
		constexpr float kNoSpacing = 0.0f;
		constexpr float kNoBorder = 0.0f;
		constexpr int kInputStyleVarCount = 2;
		constexpr float kNarrowest = 1.0f;
		const ImVec4 kClearColour(0.0f, 0.0f, 0.0f, 0.0f);

		using InputCall = std::function<bool(ImGuiInputTextFlags)>;
		using ClearCall = std::function<void()>;

		float ClearButtonInset()
		{
			return std::floor((ImGui::GetFrameHeight() - Core::IconButtonSize()) * kHalf);
		}

		// WARNING: The frame is drawn here across the whole width and the input's own frame is cleared, so the
		// clear button sits inside one field rather than beside it. InputText fills its frame with FrameBg whether
		// hovered or typed in, so this frame does too.
		bool DrawField(const char* id, bool hasText, const FilterFieldLook& look, const InputCall& input,
					   const ClearCall& clear)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const ImVec2 framePadding = style.FramePadding;
			const float fieldRounding = style.FrameRounding;

			// WARNING: Floored, as ImGui lays out an item's width, so the frame ends where the field does. And never
			// below a pixel: a width of zero or less would turn the frame inside out, and given to SetNextItemWidth
			// it would mean the rest of the line less that much.
			const float width = std::max(kNarrowest, std::floor(look.Width.value_or(ImGui::GetContentRegionAvail().x)));
			const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
			const ImVec2 fieldMax(fieldMin.x + width, fieldMin.y + ImGui::GetFrameHeight());

			const float glyphWidth = ImGui::CalcTextSize(look.Glyph).x;
			const float textLeft = fieldMin.x + framePadding.x * 2.0f + glyphWidth;
			const float clearSlotWidth = Core::IconButtonSize() + ClearButtonInset();
			const bool showsClear = hasText && width > clearSlotWidth;

			ImDrawList* const drawList = ImGui::GetWindowDrawList();
			constexpr bool kWithBorder = true;
			ImGui::RenderFrame(fieldMin, fieldMax, ImGui::GetColorU32(ImGuiCol_FrameBg), kWithBorder, fieldRounding);

			constexpr bool kWithinWhatIsShown = true;
			drawList->PushClipRect(fieldMin, fieldMax, kWithinWhatIsShown);
			drawList->AddText(ImVec2(fieldMin.x + framePadding.x, fieldMin.y + framePadding.y),
							  ImGui::GetColorU32(ImGuiCol_TextDisabled), look.Glyph);
			drawList->PopClipRect();

			ImGui::PushStyleColor(ImGuiCol_FrameBg, kClearColour);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, kNoBorder);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadding.x * 2.0f + glyphWidth, framePadding.y));
			ImGui::SetNextItemWidth(showsClear ? width - clearSlotWidth : width);

			// WARNING: Clipped from where the text starts. Once a long query scrolls, InputText draws it, and its
			// selection, from the frame's own edge, which here is under the glyph.
			drawList->PushClipRect(ImVec2(textLeft, fieldMin.y), fieldMax, kWithinWhatIsShown);
			bool edited = input(ImGuiInputTextFlags_EscapeClearsAll);
			drawList->PopClipRect();

			ImGui::PopStyleVar(kInputStyleVarCount);
			ImGui::PopStyleColor();

			if (!showsClear)
			{
				return edited;
			}

			ImGui::SameLine(kRightAfter, kNoSpacing);
			ImGui::PushID(id);

			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(0.0f, fieldRounding - ClearButtonInset()));

			if (Core::DrawIconButton("##clear", ICON_MDI_CLOSE, kNotEngaged, "Clear"))
			{
				clear();
				edited = true;
			}

			ImGui::PopStyleVar();
			ImGui::PopID();

			// WARNING: Only laid out, not a Dummy: an item here would take the button's place as the last item, which a
			// caller asking IsItemHovered after the field expects to be the button.
			ImGui::SameLine(kRightAfter, kNoSpacing);
			ImGui::ItemSize(ImVec2(ClearButtonInset(), ImGui::GetFrameHeight()));

			return edited;
		}
	}

	bool DrawFilterField(const char* id, char* buffer, std::size_t bufferSize, const FilterFieldLook& look)
	{
		const bool hasText = buffer[0] != '\0';

		return DrawField(
			id, hasText, look,
			[&](ImGuiInputTextFlags flags) { return ImGui::InputTextWithHint(id, look.Hint, buffer, bufferSize, flags); },
			[&] { buffer[0] = '\0'; });
	}

	bool DrawFilterField(const char* id, std::string& text, const FilterFieldLook& look)
	{
		return DrawField(
			id, !text.empty(), look,
			[&](ImGuiInputTextFlags flags) { return Core::InputTextWithHint(id, look.Hint, text, flags); },
			[&] { text.clear(); });
	}

	float FilterFieldWidthBefore(int iconButtons)
	{
		const float roomPerButton = Core::IconButtonSize() + ImGui::GetStyle().ItemSpacing.x;
		return ImGui::GetContentRegionAvail().x - roomPerButton * static_cast<float>(iconButtons);
	}
}
