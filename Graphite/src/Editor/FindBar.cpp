#include "Editor/FindBar.h"

#include "Editor/FindNavigation.h"

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

		// The find bar's own left and right inset. Its vertical padding is zero: the bar is one
		// frame tall, which is exactly what the widgets inside it need and nothing more.
		constexpr float kFindBarPaddingX = 6.0f;

		// What the bar keeps clear of the editor's top and right edges, and of the scrollbar when
		// one is showing. A floating box wedged into the corner reads as a rendering mistake.
		constexpr float kFindBarMargin = 6.0f;

		constexpr float kFindSeparatorWidth = 1.0f;

		// The scheme describes what the editor draws, not the chrome around it, so it has no
		// error entry to read — this is the red the console marks an error with.
		ImVec4 NoMatchColor()
		{
			return ImVec4(0.94f, 0.42f, 0.42f, 1.0f);
		}

		void CloseFindBar(Core::OpenItem& item)
		{
			item.Find.Open = false;

			// The highlight draws straight from this list, so a closed bar has to leave none
			// behind. The query survives, so reopening the bar offers it back.
			item.Find.Matches.clear();
			item.Find.Current = 0;
		}

		// Room for a query worth typing, and no more: the bar hangs off the editor's right corner
		// and every pixel it takes is a pixel of code it covers. Measured in characters of the
		// editor's own font, so the bar keeps its proportions when the font size changes.
		float FindFieldWidth()
		{
			return ImGui::CalcTextSize("0").x * 14.0f;
		}

		// Smaller than a row, so the buttons sit inside the bar with air around them rather than
		// filling it edge to edge.
		float FindButtonSize()
		{
			return std::floor(ImGui::GetFontSize() + 4.0f);
		}

		// Drawn rather than submitted as an ImGui::Button, which is what put the glyphs off centre:
		// Button centres its label inside the frame LESS FramePadding, and on a square the width of
		// a row the horizontal padding leaves an inner box narrower than the glyph — so the text no
		// longer fits, and RenderTextClipped falls back to aligning it left.
		//
		// The same shape TitleBar::CaptionButton already uses for its window buttons.
		bool FindIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip)
		{
			const float size = FindButtonSize();

			// Centred in the row the field sets the height of.
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
								 std::floor((ImGui::GetFrameHeight() - size) * 0.5f));

			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + size, min.y + size);

			const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
			const bool held = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();

			ImDrawList* draw = ImGui::GetWindowDrawList();

			// No fill at rest: five boxed buttons beside a text field read as a form. An engaged
			// toggle keeps one, because "on" has to be visible without hovering it.
			if (held || hovered || engaged)
			{
				const ImU32 fill = held	   ? ImGui::GetColorU32(ImGuiCol_ButtonActive)
								   : hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
											 : ImGui::GetColorU32(ImGuiCol_Button);

				draw->AddRectFilled(min, max, fill, ImGui::GetStyle().FrameRounding);
			}

			// Floored, not rounded: CalcTextSize rounds up, so a glyph centred onto a half pixel
			// is resampled and reads blurrier than the text beside it.
			const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
			const ImVec2 at(std::floor(min.x + (size - glyphSize.x) * 0.5f),
							std::floor(min.y + (size - glyphSize.y) * 0.5f));

			// The accent is the second signal that a toggle is on: the darker fill alone is what
			// ImGui uses for a button being pressed right now, so on and under-the-mouse would
			// otherwise look the same.
			draw->AddText(at, ImGui::GetColorU32(engaged ? ImGuiCol_CheckMark : ImGuiCol_Text),
						  glyph);

			// The icon says nothing to someone meeting it for the first time, so this is not
			// decoration — it is the only place the button explains itself.
			ImGui::SetItemTooltip("%s", tooltip);

			return pressed;
		}

		// A hairline between what you type into and what you press, shorter than the bar so it
		// reads as a divider rather than as a wall. Drawn rather than submitted for the same
		// reason the icons are: a separator here has to be centred against the field's height,
		// which ImGui's own vertical separator takes from the layout instead.
		void DrawFindSeparator()
		{
			const float height = std::floor(ImGui::GetFontSize() * 0.9f);

			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float top = at.y + std::floor((ImGui::GetFrameHeight() - height) * 0.5f);

			ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, top), ImVec2(at.x, top + height),
											   ImGui::GetColorU32(ImGuiCol_Separator));

			// Claims the line it drew on, so the spacing either side of it is the ordinary item
			// spacing and nothing has to know a line went past.
			ImGui::Dummy(ImVec2(kFindSeparatorWidth, ImGui::GetFrameHeight()));
		}

		bool DrawFindToggle(const char* id, const char* icon, const char* tooltip, bool& value)
		{
			const bool clicked = FindIconButton(id, icon, value, tooltip);

			if (clicked)
				value = !value;

			return clicked;
		}

		// No spaces around the slash. The counter's box is reserved at its widest and paid for on
		// every search, so two characters of padding inside it are two characters of code the bar
		// covers for the whole time it is open.
		std::string MatchCounterText(const Core::FindState& find)
		{
			if (find.Matches.empty())
				return "0/0";

			return std::to_string(find.Current + 1) + "/" + std::to_string(find.Matches.size());
		}

		// A constant, and reserved even while the counter shows nothing. It depends on neither the
		// query nor the number of matches on purpose: the bar hangs off the editor's right corner,
		// so anything that changes its width moves its left edge — and a counter that appeared
		// with the first keystroke, then widened again at the tenth match and the hundredth, made
		// the whole bar jump sideways while being typed into.
		//
		// Four digits either side covers a single file. Past that the text is wider than its box
		// and runs into the separator, which is the one case where too much is preferable to a bar
		// that resizes for everyone else.
		float MatchCounterWidth()
		{
			return ImGui::CalcTextSize("9999/9999").x;
		}

		// Drawn inside the query field, against its right edge, rather than as an item beside it.
		//
		// Beside it, the counter floated: the field is a fixed fourteen characters and a short
		// query leaves most of it empty, so a counter placed after the field sat a wide gap away
		// from the text it was counting no matter which way it was aligned within its own box.
		// Anchoring it to the field removes the gap by removing the box.
		//
		// A filled rect under it, in the field's own background colour, because a long query
		// scrolls its tail to exactly this corner — the counter has to sit on top of it rather
		// than blend into it.
		void DrawMatchCounterInField(const Core::FindState& find, const ImVec2& fieldMin,
									 const ImVec2& fieldMax)
		{
			// Nothing while the query is empty: "0/0" beside an empty field reports a failure
			// nobody has asked for yet. Costs no room either way — the field reserved it.
			if (find.Query.empty())
				return;

			const std::string counter = MatchCounterText(find);
			const ImVec2 size = ImGui::CalcTextSize(counter.c_str());

			const float padding = ImGui::GetStyle().FramePadding.x;
			const float left = fieldMax.x - padding - size.x;

			// Centred against the field's own height rather than offset by its padding. The
			// padding is not square — the horizontal is the larger of the two — so using it for
			// the vertical, as this first did, drops the counter below the query beside it.
			const float top = fieldMin.y + (fieldMax.y - fieldMin.y - size.y) * 0.5f;

			ImDrawList* draw = ImGui::GetWindowDrawList();

			draw->AddRectFilled(ImVec2(left - padding, fieldMin.y + 1.0f),
								ImVec2(fieldMax.x - 1.0f, fieldMax.y - 1.0f),
								ImGui::GetColorU32(ImGuiCol_FrameBg));

			// Floored for the same reason the icons are: CalcTextSize rounds up, and text landed
			// on a half pixel is resampled and reads blurrier than the query beside it.
			draw->AddText(ImVec2(std::floor(left), std::floor(top)),
						  ImGui::GetColorU32(ImGuiCol_TextDisabled), counter.c_str());
		}

		struct FindBarFrame
		{
			// The visible top-RIGHT of the editor surface, which is the window's own corner and
			// not the content's: the content origin scrolls away upwards, and a bar pinned to it
			// would scroll off with the text.
			//
			// The right corner rather than the left because the bar is compact and hangs off it.
			// It takes no room out of the document, so the code never moves when it opens; what
			// it covers is the tail of the first rows, which is where a line has usually already
			// ended.
			ImVec2 Corner;

			// How much room there is to hang it in. The bar sizes itself to its contents and is
			// clamped to this, so a panel dragged narrow gives up query field rather than
			// spilling its buttons past the edge.
			float Width = 0.0f;

			// Root and children, so the bar's own field counts as this document holding the
			// keyboard — Escape and F3 are as much the field's keys as the document's.
			bool DocumentFocused = false;
		};

		// The longest query the field takes. A find query is something typed by hand; a limit
		// this side of a paragraph costs nothing and keeps the field off the heap.
		constexpr std::size_t kFindQueryLimit = 256;

		// What the selection is worth seeding the query with, and nothing when it is worth
		// nothing. Both refusals leave the previous query standing rather than offering a
		// trimmed one: a selection longer than the field would be searched for in full while
		// showing only its first part, and a selection spanning lines cannot match at all —
		// FindInBuffer works a line at a time, so a query holding a newline reads "0 / 0" in red
		// whatever those lines say. Cutting either down to the part that fits would search for
		// something the user did not select and blame them for the result.
		std::optional<std::string> SelectionAsQuery(const Core::TextBuffer& buffer,
												   const Core::Caret& caret)
		{
			if (!Core::HasSelection(caret))
				return std::nullopt;

			const std::pair<std::size_t, std::size_t> selection = Core::SelectionRange(caret);

			// Measured before the text is taken: a selection of half a file has no business
			// being copied out only to be thrown away.
			if (selection.second - selection.first >= kFindQueryLimit)
				return std::nullopt;

			std::string text = buffer.Substring(selection.first, selection.second);

			if (text.find('\n') != std::string::npos)
				return std::nullopt;

			return text;
		}

		// Where the bar hangs, worked out from the surface it is being drawn into rather than
		// handed in: only the caller knows whether the document has the keyboard, and only this
		// file knows what the bar needs geometrically.
		FindBarFrame FrameForCurrentWindow(bool documentFocused)
		{
			FindBarFrame frame;

			// Less whatever the vertical scrollbar is taking and a margin, so the bar hangs beside
			// the scrollbar with air around it rather than wedged against it and against the top.
			const float usable = ImGui::GetWindowWidth() -
								 ImGui::GetCurrentWindow()->ScrollbarSizes.x - kFindBarMargin;

			frame.Corner = ImVec2(ImGui::GetWindowPos().x + usable,
								  ImGui::GetWindowPos().y + kFindBarMargin);
			frame.Width = usable - kFindBarMargin;
			frame.DocumentFocused = documentFocused;

			return frame;
		}
	}

	void FindBar::Open(Core::OpenItem& item)
	{
		if (!item.Document)
			return;

		const Core::TextBuffer& buffer = item.Document->GetBuffer();

		// A selection is what the user is already looking at, so it is what they mean to search
		// for; without one the last query stands, which is what makes Ctrl+F twice in a row a
		// way back to the same search.
		if (const auto seed = SelectionAsQuery(buffer, item.Caret))
		{
			item.Find.Query = *seed;
		}

		item.Find.Open = true;

		// Built here rather than left to the frame: the revision test alone cannot tell a list
		// built for another query from a current one, so the query's every change rebuilds as it
		// happens, and seeding one is a change.
		RefreshFindMatches(item, buffer);

		m_FocusFor = item.Path;
	}

	void FindBar::Draw(Core::OpenItem& item, const Core::TextBuffer& buffer, bool documentFocused)
	{
		const FindBarFrame frame = FrameForCurrentWindow(documentFocused);

		const bool noMatches = item.Find.Matches.empty() && !item.Find.Query.empty();

		// Read before the field is submitted so that the press which closes the bar below and
		// the press which cancels the write-back are one and the same reading, not two that
		// could drift apart. IsKeyPressed answers for the whole frame and ignores ownership,
		// so asking early is the same question asked late.
		const bool escapePressed =
			frame.DocumentFocused && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// BeginChild sizes its item like any other, which pushes the surface's CursorMaxPos
		// out to the bar's far corner — and the surface's extent is what its scrollbars are
		// decided from, so an overlay pinned to the window's own edges would make the content
		// exactly as wide as the window and raise a horizontal scrollbar on every document
		// narrower than one. Restored after EndChild, which leaves ReserveDocumentExtent's
		// Dummy the only thing in this file that grows the extent.
		const ImVec2 extentBeforeBar = ImGui::GetCurrentWindow()->DC.CursorMaxPos;

		// Measured before the bar is opened, because it decides how wide the bar is. The five
		// controls are square cells of one row's height, so the whole thing is arithmetic
		// rather than a guess — which is what stops the last button being clipped by the
		// bar's own edge.
		const ImGuiStyle& style = ImGui::GetStyle();
		const float square = FindButtonSize();
		const float counterWidth = MatchCounterWidth();

		// The field swallows the counter's room, because the counter is drawn inside it.
		//
		// No conditional term anywhere in this sum: every part of it is a constant for a given
		// font, so the bar has one width for the whole time it is open and its left edge never
		// moves.
		const float fieldWidth = FindFieldWidth() + counterWidth;

		const float barWidth =
			std::min(kFindBarPaddingX * 2.0f + fieldWidth + style.ItemSpacing.x +
						 kFindSeparatorWidth + (square + style.ItemSpacing.x) * 5.0f,
					 frame.Width);

		ImGui::SetCursorScreenPos(ImVec2(frame.Corner.x - barWidth, frame.Corner.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kFindBarPaddingX, 0.0f));

		// Opaque, and drawn as a child window rather than as a rectangle on the editor's own
		// draw list: a nested child is painted above everything its parent drew, so the text
		// behind the bar cannot show through whatever order the rows were submitted in.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));

		const bool visible =
			ImGui::BeginChild("##FindBar", ImVec2(barWidth, ImGui::GetFrameHeight()),
							  ImGuiChildFlags_Borders,
							  ImGuiWindowFlags_NoScrollWithMouse);

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		if (!visible)
		{
			ImGui::EndChild();
			ImGui::GetCurrentWindow()->DC.CursorMaxPos = extentBeforeBar;
			return;
		}

		std::array<char, kFindQueryLimit> field{};

		item.Find.Query.copy(field.data(), std::min(item.Find.Query.size(), field.size() - 1));

		if (m_FocusFor == item.Path)
		{
			// SetKeyboardFocusHere only takes effect on the very next widget submitted.
			ImGui::SetKeyboardFocusHere();
			m_FocusFor.clear();
		}

		if (noMatches)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, NoMatchColor());
		}

		// What is left after the controls, which is FindFieldWidth() at any width the bar was
		// not clamped at — and less than it when the panel is too narrow, so a cramped bar
		// gives up field rather than spilling its buttons past its own edge.
		const float controls =
			style.ItemSpacing.x + kFindSeparatorWidth + (square + style.ItemSpacing.x) * 5.0f;

		ImGui::SetNextItemWidth(
			std::max(ImGui::GetContentRegionAvail().x - controls, ImGui::GetFontSize()));

		// EnterReturnsTrue rather than a bare field: a single-line InputText swallows Enter
		// whatever the flag says, and this is what hands it back to be stepped on.
		const bool entered =
			ImGui::InputTextWithHint("##findQuery", "Find", field.data(), field.size(),
									 ImGuiInputTextFlags_EnterReturnsTrue);

		const bool queryEdited =
			AcceptsQueryEdit(FindFieldEvents{ImGui::IsItemEdited(), escapePressed});

		// Read from the item just submitted, so the counter lands in the field whatever the
		// layout did with it.
		const ImVec2 fieldMin = ImGui::GetItemRectMin();
		const ImVec2 fieldMax = ImGui::GetItemRectMax();

		if (noMatches)
		{
			ImGui::PopStyleColor();
		}

		if (queryEdited)
		{
			item.Find.Query = field.data();
			RefreshFindMatches(item, buffer);
		}

		DrawMatchCounterInField(item.Find, fieldMin, fieldMax);

		// The field and the count it carries are one thing; the buttons that act on them are
		// another.
		ImGui::SameLine();
		DrawFindSeparator();
		ImGui::SameLine();

		const bool matchCaseToggled = DrawFindToggle("##findCase", ICON_MDI_FORMAT_LETTER_CASE,
													 "Match case", item.Find.MatchCase);

		ImGui::SameLine();

		const bool wholeWordToggled =
			DrawFindToggle("##findWord", ICON_MDI_FORMAT_LETTER_MATCHES, "Whole word",
						   item.Find.WholeWord);

		if (matchCaseToggled || wholeWordToggled)
			RefreshFindMatches(item, buffer);

		ImGui::SameLine();

		const bool previousClicked =
			FindIconButton("##findPrev", ICON_MDI_CHEVRON_UP, false, "Previous (Shift+Enter)");

		ImGui::SameLine();

		const bool nextClicked =
			FindIconButton("##findNext", ICON_MDI_CHEVRON_DOWN, false, "Next (Enter)");

		ImGui::SameLine();

		const bool closeClicked =
			FindIconButton("##findClose", ICON_MDI_CLOSE, false, "Close (Escape)");

		ImGui::EndChild();
		ImGui::GetCurrentWindow()->DC.CursorMaxPos = extentBeforeBar;

		const bool shift = ImGui::GetIO().KeyShift;

		// F3 wherever the document has the keyboard, Enter only from the field: Enter in the
		// document is a newline, and one press doing both would step the find and edit the
		// file.
		const bool f3 = frame.DocumentFocused && ImGui::IsKeyPressed(ImGuiKey_F3);
		const bool stepBack = previousClicked || (shift && (entered || f3));
		const bool stepOn = nextClicked || (!shift && (entered || f3));

		if (closeClicked || escapePressed)
		{
			// Leaves the caret where the last step put it, which is the point of closing on
			// the match rather than jumping back to where the search began.
			CloseFindBar(item);
			return;
		}

		if (stepBack || stepOn)
		{
			StepFindMatch(item, buffer, stepBack);

			// Enter deactivates a single-line field whatever else it does, so without this
			// the second Enter of a run would land in the document instead of the bar.
			m_FocusFor = item.Path;
		}
	}
}
