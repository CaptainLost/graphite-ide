#include "Editor/Find/FindBar.h"

#include "Editor/Find/FindNavigation.h"
#include "Theme/Palettes.h"
#include "Core/ImGui/IconButton.h"

#include "Core/ImGui/UiScale.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextBuffer.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace Graphite {

	namespace {

		constexpr float kFindBarPaddingX = 6.0f;
		constexpr float kFindBarPaddingY = 0.0f;
		constexpr float kFindBarMargin = 6.0f;
		constexpr float kFindBarPaddingSides = 2.0f;
		constexpr float kFindFieldCharacters = 28.0f;
		constexpr float kButtonsRightOfCounter = 5.0f;
		constexpr float kHalf = 0.5f;
		constexpr int kPreviousItem = -1;
		constexpr bool kNotToggled = false;
		constexpr std::size_t kFindQueryLimit = 256;

		void CloseFindBar(Core::OpenItem& item)
		{
			item.Find.Open = false;
			item.Find.Matches.clear();
			item.Find.CurrentMatch = 0;
		}

		float FindFieldWidth()
		{
			return ImGui::CalcTextSize("0").x * kFindFieldCharacters;
		}

		bool DrawFindToggle(const char* id, const char* icon, const char* tooltip, bool& value)
		{
			const bool clicked = Core::DrawIconButton(id, icon, value, tooltip);

			if (clicked)
			{
				value = !value;
			}

			return clicked;
		}

		std::string MatchCounterText(const Core::FindState& find)
		{
			if (find.Matches.empty())
			{
				return "0/0";
			}

			return std::to_string(find.CurrentMatch + 1) + "/" + std::to_string(find.Matches.size());
		}

		float MatchCounterWidth()
		{
			return ImGui::CalcTextSize("999/999").x;
		}

		void DrawMatchCounter(const Core::FindState& find, float width)
		{
			if (!find.Query.empty())
			{
				const std::string counterText = MatchCounterText(find);
				const ImVec2 textSize = ImGui::CalcTextSize(counterText.c_str());

				const ImVec2 counterOrigin = ImGui::GetCursorScreenPos();

				// WARNING: CalcTextSize rounds up, and text landed on a half pixel is resampled and
				// reads blurrier than the query beside it, so the position is floored.
				const float textLeft = std::floor(counterOrigin.x + width - textSize.x);
				const float textTop =
					std::floor(counterOrigin.y + (ImGui::GetFrameHeight() - textSize.y) * kHalf);

				ImGui::GetWindowDrawList()->AddText(ImVec2(textLeft, textTop),
													ImGui::GetColorU32(ImGuiCol_TextDisabled),
													counterText.c_str());
			}

			ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
		}

		struct FindBarWidths
		{
			float Field = 0.0f;
			float Counter = 0.0f;
			float RightOfField = 0.0f;
			float Total = 0.0f;
		};

		FindBarWidths MeasureFindBar()
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			FindBarWidths widths;

			widths.Field = FindFieldWidth();
			widths.Counter = MatchCounterWidth();

			widths.RightOfField = style.ItemSpacing.x + Core::Scaled(Core::kIconSeparatorWidth) +
								  style.ItemSpacing.x + widths.Counter +
								  (style.ItemSpacing.x + Core::IconButtonSize()) * kButtonsRightOfCounter;

			widths.Total = Core::Scaled(kFindBarPaddingX) * kFindBarPaddingSides + widths.Field +
						   widths.RightOfField;

			return widths;
		}

		struct FindBarFrame
		{
			ImVec2 Corner;
			float Width = 0.0f;
			bool DocumentFocused = false;
		};

		std::optional<std::string> SelectionAsQuery(const Core::TextBuffer& buffer,
													const Core::Caret& caret)
		{
			if (!Core::HasSelection(caret))
			{
				return std::nullopt;
			}

			const Core::OffsetRange selection = Core::SelectionRange(caret);

			if (selection.To - selection.From >= kFindQueryLimit)
			{
				return std::nullopt;
			}

			std::string text = buffer.Substring(selection.From, selection.To);

			if (text.find('\n') != std::string::npos)
			{
				return std::nullopt;
			}

			return text;
		}

		FindBarFrame FrameForCurrentWindow(bool documentFocused)
		{
			FindBarFrame frame;

			const float usableWidth = ImGui::GetWindowWidth() -
									  ImGui::GetCurrentWindow()->ScrollbarSizes.x -
									  Core::Scaled(kFindBarMargin);

			frame.Corner = ImVec2(ImGui::GetWindowPos().x + usableWidth,
								  ImGui::GetWindowPos().y + Core::Scaled(kFindBarMargin));
			frame.Width = usableWidth - Core::Scaled(kFindBarMargin);
			frame.DocumentFocused = documentFocused;

			return frame;
		}
	}

	void FindBar::Open(Core::OpenItem& item)
	{
		if (!item.Document)
		{
			return;
		}

		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		if (const std::optional<std::string> seed = SelectionAsQuery(buffer, item.Caret))
		{
			item.Find.Query = *seed;
		}

		item.Find.Open = true;

		RefreshFindMatches(item, buffer);
		RevealFindMatch(item, buffer);

		m_FocusFor = item.Path;
	}

	void FindBar::Draw(Core::OpenItem& item, const Core::TextBuffer& buffer, bool documentFocused)
	{
		const FindBarFrame frame = FrameForCurrentWindow(documentFocused);

		const bool noMatches = item.Find.Matches.empty() && !item.Find.Query.empty();

		const bool escapePressed = frame.DocumentFocused && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// WARNING: BeginChild sizes its item like any other and pushes the surface's CursorMaxPos
		// out to the bar's far corner, which would raise a horizontal scrollbar on every document
		// narrower than the window. The extent is restored after EndChild.
		const ImVec2 extentBeforeBar = ImGui::GetCurrentWindow()->DC.CursorMaxPos;

		const FindBarWidths widths = MeasureFindBar();

		const float barWidth = std::min(widths.Total, frame.Width);

		ImGui::SetCursorScreenPos(ImVec2(frame.Corner.x - barWidth, frame.Corner.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
							ImVec2(Core::Scaled(kFindBarPaddingX), kFindBarPaddingY));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

		const bool barVisible =
			ImGui::BeginChild("##FindBar", ImVec2(barWidth, ImGui::GetFrameHeight()),
							  ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		if (!barVisible)
		{
			ImGui::EndChild();
			ImGui::GetCurrentWindow()->DC.CursorMaxPos = extentBeforeBar;
			return;
		}

		std::array<char, kFindQueryLimit> queryBuffer{};

		item.Find.Query.copy(queryBuffer.data(),
							 std::min(item.Find.Query.size(), queryBuffer.size() - 1));

		if (m_FocusFor == item.Path)
		{
			// WARNING: SetKeyboardFocusHere only takes effect on the very next widget submitted.
			ImGui::SetKeyboardFocusHere();
			m_FocusFor.clear();
		}

		if (noMatches)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kErrorTextColour);
		}

		ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - widths.RightOfField,
										 ImGui::GetFontSize()));

		// WARNING: A single-line InputText swallows Enter whatever the flags say; EnterReturnsTrue
		// is what hands it back to be stepped on.
		const bool enterPressed =
			ImGui::InputTextWithHint("##findQuery", "Find", queryBuffer.data(), queryBuffer.size(),
									 ImGuiInputTextFlags_EnterReturnsTrue);

		const bool queryEdited = AcceptsQueryEdit(
			FindFieldEvents{.Edited = ImGui::IsItemEdited(), .EscapePressed = escapePressed});

		if (noMatches)
		{
			ImGui::PopStyleColor();
		}

		if (enterPressed)
		{
			// WARNING: Enter deactivates a single-line field. Focus is requested here, for the item
			// just submitted, because a request routed through m_FocusFor lands a frame later and
			// the keystroke arriving in between is lost.
			ImGui::SetKeyboardFocusHere(kPreviousItem);
		}

		if (queryEdited)
		{
			item.Find.Query = queryBuffer.data();
			RefreshFindMatches(item, buffer);
			RevealFindMatch(item, buffer);
		}

		ImGui::SameLine();
		Core::DrawIconSeparator();
		ImGui::SameLine();

		DrawMatchCounter(item.Find, widths.Counter);

		ImGui::SameLine();

		const bool matchCaseToggled = DrawFindToggle("##findCase", ICON_MDI_FORMAT_LETTER_CASE,
													 "Match case", item.Find.MatchCase);

		ImGui::SameLine();

		const bool wholeWordToggled = DrawFindToggle(
			"##findWord", ICON_MDI_FORMAT_LETTER_MATCHES, "Whole word", item.Find.WholeWord);

		if (matchCaseToggled || wholeWordToggled)
		{
			RefreshFindMatches(item, buffer);
			RevealFindMatch(item, buffer);
		}

		ImGui::SameLine();

		const bool previousClicked = Core::DrawIconButton("##findPrev", ICON_MDI_CHEVRON_UP, kNotToggled,
													"Previous (Shift+Enter)");

		ImGui::SameLine();

		const bool nextClicked =
			Core::DrawIconButton("##findNext", ICON_MDI_CHEVRON_DOWN, kNotToggled, "Next (Enter)");

		ImGui::SameLine();

		const bool closeClicked =
			Core::DrawIconButton("##findClose", ICON_MDI_CLOSE, kNotToggled, "Close (Escape)");

		ImGui::EndChild();
		ImGui::GetCurrentWindow()->DC.CursorMaxPos = extentBeforeBar;

		const bool shiftHeld = ImGui::GetIO().KeyShift;

		const bool f3Pressed = frame.DocumentFocused && ImGui::IsKeyPressed(ImGuiKey_F3);
		const bool stepBack = previousClicked || (shiftHeld && (enterPressed || f3Pressed));
		const bool stepOn = nextClicked || (!shiftHeld && (enterPressed || f3Pressed));

		if (closeClicked || escapePressed)
		{
			CloseFindBar(item);
			return;
		}

		if (stepBack || stepOn)
		{
			const StepDirection direction =
				stepBack ? StepDirection::Backwards : StepDirection::Forwards;

			StepFindMatch(item, buffer, direction);
		}

		if (previousClicked || nextClicked)
		{
			m_FocusFor = item.Path;
		}
	}
}
