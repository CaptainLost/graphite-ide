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
namespace Graphite {

	// A type of static members rather than a namespace of its own: the house style keeps one
	// namespace per project.
	class DialogLayout final
	{
	public:
		DialogLayout() = delete;

		// At 100%; each goes through Core::Scaled where it is used.
		static constexpr float kNarrowWidth = 460.0f;
		static constexpr float kWideWidth = 600.0f;
		static constexpr float kPadding = 16.0f;
		static constexpr float kSectionGap = 6.0f;
		static constexpr float kButtonMinWidth = 90.0f;
		static constexpr std::size_t kListRows = 12;

		// The theme gives frames and popups the same surface colour, so a checkbox or a field in
		// a modal had no edge at all. In a modal they take the window background instead: a well
		// sunk into the popup, the same contrast a field in a panel has the other way round.
		// ModalQuestion pushes this for every question it asks.
		static void PushModalFrameColours()
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
		}

		static void PopModalFrameColours()
		{
			ImGui::PopStyleColor();
		}

		// Around the question's Begin. The padding is read by Begin and must not reach the
		// children drawn inside, so it is popped straight after; the width is fixed so the dialog
		// does not jump with the length of what it names.
		static void BeforeBegin(float width)
		{
			const float scaled = Core::Scaled(width);
			ImGui::SetNextWindowSizeConstraints(ImVec2(scaled, 0.0f), ImVec2(scaled, FLT_MAX));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Core::Scaled(ImVec2(kPadding, kPadding)));
		}

		static void AfterBegin()
		{
			ImGui::PopStyleVar();
		}

		// Air between two sections: the text, the list, the options, the buttons.
		static void Gap()
		{
			ImGui::Dummy(ImVec2(0.0f, Core::Scaled(kSectionGap)));
		}

		// Wrapped at the dialog's edge, which the fixed width keeps where it is.
		static void Text(const std::string& text)
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopTextWrapPos();
		}

		static void MutedText(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			Text(text);
			ImGui::PopStyleColor();
		}

		// Drawn only when there is something to say: the dialog grows by a line then, rather
		// than carrying an empty one the rest of the time.
		static void ErrorText(const std::string& text, const ImVec4& colour)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			Text(text);
			ImGui::PopStyleColor();
		}

		// The group headers' size against the rows'.
		static constexpr float kHeaderScale = 0.85f;

		// A bordered, rounded list sized to its `rows` and its group `headers` - each measured at
		// its own height - and scrolling past kListRows rows' worth rather than pushing the
		// buttons off screen.
		static bool BeginFileList(const char* id, std::size_t rows, std::size_t headers)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float rowHeight = ImGui::GetTextLineHeightWithSpacing();

			// Measured in the header's own font rather than as FontSize * kHeaderScale: ImGui
			// rounds a pushed size, and a list a pixel short shows a scrollbar with nothing to
			// scroll.
			ImGui::PushFont(nullptr, style.FontSizeBase * kHeaderScale);
			const float headerHeight = ImGui::GetTextLineHeightWithSpacing();
			ImGui::PopFont();

			const float content = static_cast<float>(rows) * rowHeight + static_cast<float>(headers) * headerHeight;
			const float cap = static_cast<float>(kListRows) * rowHeight;
			const float height =
				std::clamp(content, rowHeight, cap) - style.ItemSpacing.y + style.WindowPadding.y * 2.0f;

			return ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_Borders);
		}

		static void EndFileList()
		{
			ImGui::EndChild();
		}

		// "CHANGED", "UNTRACKED": small and muted, so the paths are what the eye lands on.
		static void FileGroupHeader(const char* text)
		{
			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kHeaderScale);
			ImGui::TextDisabled("%s", text);
			ImGui::PopFont();
		}

		// One row: the icon, the path - cut short with an ellipsis, whole in a tooltip - and,
		// right-aligned and muted, what makes the row matter. `tag` may be empty.
		static void FileRow(const char* icon, const std::string& path, const std::string& tag)
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
			Destructive,

			// The plain button beside Cancel, for a way out that is neither of the two roles.
			Alternative
		};

	private:
		static ImVec4 Brightened(const ImVec4& colour, float factor)
		{
			return ImVec4(std::min(colour.x * factor, 1.0f), std::min(colour.y * factor, 1.0f),
						  std::min(colour.z * factor, 1.0f), colour.w);
		}

		static bool FilledButton(const char* label, const ImVec4& fill, float width, bool enabled)
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

	public:
		// The dialog's last row: right-aligned, every button as wide as the widest, in the order
		// Cancel, the safe action in the accent colour, the destructive one in `danger`. Escape is
		// Cancel; Enter is the safe action, never the destructive one, and nothing at all in a
		// dialog without a safe action. A click wins over a key pressed in the same frame.
		//
		// `alternative`, when it has a label, is a third way out drawn unfilled between Cancel
		// and the safe action.
		static Choice ButtonRow(ButtonSpec safe, ButtonSpec destructive, const ImVec4& danger,
								ButtonSpec alternative = {})
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			constexpr const char* cancelLabel = "Cancel";

			float labelWidth = ImGui::CalcTextSize(cancelLabel).x;
			int count = 1;

			for (const ButtonSpec& spec : {safe, destructive, alternative})
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

			const bool safeOffered = safe.Label != nullptr && safe.Enabled;

			if (ImGui::Button(cancelLabel, ImVec2(buttonWidth, 0.0f)))
			{
				clicked = Choice::Cancel;
			}

			// Where the keyboard starts when the dialog opens - ImGui would otherwise put it on the
			// first item, a checkbox that Enter or Space would then flip. Cancel when there is no
			// safe action to start on: never the destructive one.
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

				// The theme's accent, which it also gives the check mark.
				if (FilledButton(safe.Label, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), buttonWidth,
								 safe.Enabled))
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

			if (!enter || !safeOffered)
			{
				return Choice::None;
			}

			// With the keyboard cursor shown on another item - Discard reached with Tab, or a
			// checkbox - Enter is ImGui's to give to that item, and answering with the safe action
			// as well would act twice. A field being typed into is the exception: Enter there
			// means "done", and the field takes nothing from it.
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			const bool typing = g.NavId != 0 && (g.ActiveId == g.NavId || g.ActiveIdPreviousFrame == g.NavId);

			if (g.NavCursorVisible && g.NavId != safeId && !typing)
			{
				return Choice::None;
			}

			return Choice::Safe;
		}
	};
}
