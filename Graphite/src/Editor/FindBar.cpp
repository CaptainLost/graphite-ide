#include "Editor/FindBar.h"

#include "Editor/FindNavigation.h"
#include "UI/IconButton.h"

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

		// The find bar's own left and right inset. Its vertical padding is zero: the bar is one
		// frame tall, which is exactly what the widgets inside it need and nothing more.
		constexpr float kFindBarPaddingX = 6.0f;

		// What the bar keeps clear of the editor's top and right edges, and of the scrollbar when
		// one is showing. A floating box wedged into the corner reads as a rendering mistake.
		constexpr float kFindBarMargin = 6.0f;

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
			return ImGui::CalcTextSize("0").x * 28.0f;
		}

		bool DrawFindToggle(const char* id, const char* icon, const char* tooltip, bool& value)
		{
			const bool clicked = DrawIconButton(id, icon, value, tooltip);

			if (clicked)
			{
				value = !value;
			}

			return clicked;
		}

		// No spaces around the slash. The counter's box is reserved at its widest and paid for on
		// every search, so two characters of padding inside it are two characters of code the bar
		// covers for the whole time it is open.
		std::string MatchCounterText(const Core::FindState& find)
		{
			if (find.Matches.empty())
			{
				return "0/0";
			}

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
			return ImGui::CalcTextSize("999/999").x;
		}

		// An item of its own, not something drawn inside the field. Inside it, the counter was in
		// the way: an InputText scrolls its text and puts its caret anywhere within its own
		// frame, so a field that kept the counter's width for itself let a long query — and the
		// caret with it — run underneath. Keeping the two apart is what makes that impossible
		// rather than merely unlikely.
		//
		// Right-aligned in its box, against the buttons that act on what it counts. The box is
		// reserved at its widest and the text in it is usually three characters, so it has to
		// sit against one edge or the other, and this is the edge it belongs to.
		//
		// Drawn rather than submitted as ImGui::Text, for the same reason the icons and the
		// separator are: this has to be centred against the field's height, which the layout
		// alone does not do.
		void DrawMatchCounter(const Core::FindState& find, float width)
		{
			// Nothing while the query is empty: "0/0" beside an empty field reports a failure
			// nobody has asked for yet. Costs no room either way — the box is reserved.
			if (!find.Query.empty())
			{
				const std::string counter = MatchCounterText(find);
				const ImVec2 size = ImGui::CalcTextSize(counter.c_str());

				const ImVec2 at = ImGui::GetCursorScreenPos();

				// Floored for the same reason the icons are: CalcTextSize rounds up, and text
				// landed on a half pixel is resampled and reads blurrier than the query beside
				// it.
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(std::floor(at.x + width - size.x),
						   std::floor(at.y + (ImGui::GetFrameHeight() - size.y) * 0.5f)),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), counter.c_str());
			}

			// Claims the box whether anything was drawn in it or not, so the bar's width is the
			// same all the time it is open and its left edge never moves.
			ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
		}

		// Every horizontal part of the bar, measured once. The bar's own width and the query
		// field's are read from these same numbers rather than each summing the row for itself,
		// which is what keeps the two from drifting: the field is given what the rest did not
		// spend, so the last button can never be clipped by the bar's own edge.
		struct FindBarWidths
		{
			float Field = 0.0f;
			float Counter = 0.0f;

			// Everything to the right of the field — the separator, the counter's box and the
			// five buttons — each with the spacing that precedes it.
			float RightOfField = 0.0f;

			// What the bar asks for, its own insets included.
			float Total = 0.0f;
		};

		// No conditional term anywhere in this sum: every part of it is a constant for a given
		// font, so the bar has one width for the whole time it is open. That matters because the
		// bar hangs off the editor's right corner — anything that changes its width moves its
		// left edge, and a row that grew with the tenth match and again with the hundredth made
		// the whole bar jump sideways while being typed into.
		FindBarWidths MeasureFindBar()
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			FindBarWidths widths;

			widths.Field = FindFieldWidth();
			widths.Counter = MatchCounterWidth();

			widths.RightOfField = style.ItemSpacing.x + Core::Scaled(kIconSeparatorWidth) +
								  style.ItemSpacing.x + widths.Counter +
								  (style.ItemSpacing.x + IconButtonSize()) * 5.0f;

			widths.Total = Core::Scaled(kFindBarPaddingX) * 2.0f + widths.Field + widths.RightOfField;

			return widths;
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
			{
				return std::nullopt;
			}

			const std::pair<std::size_t, std::size_t> selection = Core::SelectionRange(caret);

			// Measured before the text is taken: a selection of half a file has no business
			// being copied out only to be thrown away.
			if (selection.second - selection.first >= kFindQueryLimit)
			{
				return std::nullopt;
			}

			std::string text = buffer.Substring(selection.first, selection.second);

			if (text.find('\n') != std::string::npos)
			{
				return std::nullopt;
			}

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
								 ImGui::GetCurrentWindow()->ScrollbarSizes.x - Core::Scaled(kFindBarMargin);

			frame.Corner = ImVec2(ImGui::GetWindowPos().x + usable,
								  ImGui::GetWindowPos().y + Core::Scaled(kFindBarMargin));
			frame.Width = usable - Core::Scaled(kFindBarMargin);
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
		RevealFindMatch(item, buffer);

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

		// Measured before the bar is opened, because it is what decides how wide the bar is.
		// Clamped to the room there is, so a panel dragged narrow gives up query field rather
		// than spilling its buttons past the edge.
		const FindBarWidths widths = MeasureFindBar();

		const float barWidth = std::min(widths.Total, frame.Width);

		ImGui::SetCursorScreenPos(ImVec2(frame.Corner.x - barWidth, frame.Corner.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Core::Scaled(kFindBarPaddingX), 0.0f));

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

		// What is left after the rest of the row, which is FindBarWidths::Field at any width the
		// bar was not clamped at, and less than it when it was. Never less than a single
		// character: a field too narrow to show one is not a field.
		ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - widths.RightOfField,
										 ImGui::GetFontSize()));

		// EnterReturnsTrue rather than a bare field: a single-line InputText swallows Enter
		// whatever the flag says, and this is what hands it back to be stepped on.
		const bool entered =
			ImGui::InputTextWithHint("##findQuery", "Find", field.data(), field.size(),
									 ImGuiInputTextFlags_EnterReturnsTrue);

		const bool queryEdited =
			AcceptsQueryEdit(FindFieldEvents{ImGui::IsItemEdited(), escapePressed});

		if (noMatches)
		{
			ImGui::PopStyleColor();
		}

		// Enter deactivates a single-line field whatever else it does, and a find run is a
		// string of Enters. Asked for here, of the item just submitted, rather than through
		// m_FocusFor on the next frame: a request aimed at the previous item is resolved this
		// frame and applied on the next, so the field is never drawn — nor read from —
		// unfocused. Through m_FocusFor there was a frame in between where the field was
		// inactive and the keystroke that fell into it was lost.
		if (entered)
		{
			ImGui::SetKeyboardFocusHere(-1);
		}

		if (queryEdited)
		{
			item.Find.Query = field.data();
			RefreshFindMatches(item, buffer);
			RevealFindMatch(item, buffer);
		}

		// What was typed on one side of the divider, what came of it on the other.
		ImGui::SameLine();
		DrawIconSeparator();
		ImGui::SameLine();

		DrawMatchCounter(item.Find, widths.Counter);

		ImGui::SameLine();

		const bool matchCaseToggled = DrawFindToggle("##findCase", ICON_MDI_FORMAT_LETTER_CASE,
													 "Match case", item.Find.MatchCase);

		ImGui::SameLine();

		const bool wholeWordToggled =
			DrawFindToggle("##findWord", ICON_MDI_FORMAT_LETTER_MATCHES, "Whole word",
						   item.Find.WholeWord);

		if (matchCaseToggled || wholeWordToggled)
		{
			RefreshFindMatches(item, buffer);
			RevealFindMatch(item, buffer);
		}

		ImGui::SameLine();

		const bool previousClicked =
			DrawIconButton("##findPrev", ICON_MDI_CHEVRON_UP, false, "Previous (Shift+Enter)");

		ImGui::SameLine();

		const bool nextClicked =
			DrawIconButton("##findNext", ICON_MDI_CHEVRON_DOWN, false, "Next (Enter)");

		ImGui::SameLine();

		const bool closeClicked =
			DrawIconButton("##findClose", ICON_MDI_CLOSE, false, "Close (Escape)");

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
		}

		// A click hands the keyboard to the button it lands on, so without this the Enter after
		// one would be the document's. Only the buttons: Enter has already re-armed the field
		// above, and F3 is answered wherever the document holds the keyboard — pressing it with
		// the caret in the text is not a request to leave it.
		if (previousClicked || nextClicked)
		{
			m_FocusFor = item.Path;
		}
	}
}
