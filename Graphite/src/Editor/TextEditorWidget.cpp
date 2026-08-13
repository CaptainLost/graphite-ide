#include "Editor/TextEditorWidget.h"

#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/FrameInput.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextDocument.h"
#include "Core/Text/WrapLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kGutterPaddingX = 8.0f;

		constexpr int kMinimumGutterDigits = 3;

		constexpr float kBreakpointColumnCells = 1.0f;

		constexpr float kFoldColumnCells = 1.0f;

		constexpr double kCaretBlinkPeriod = 1.0;

		double s_CaretMovedAt = 0.0;

		// Stepping through the history runs on its own clock rather than on the keyboard's
		// auto-repeat, which is tuned for typing: at the ~30 a second Windows delivers, a
		// held undo is a dozen changes past the one being looked for before the key comes
		// up. A history step is a whole change — a word, a line, a pasted block — so it
		// wants to be slow enough to watch and stop.
		constexpr double kHistoryHoldDelay = 0.4;

		constexpr double kHistoryFirstInterval = 0.14;

		constexpr double kHistoryFastestInterval = 0.06;

		// How much of the interval a second of holding takes away, so the stream tightens
		// from the first rate to the fastest over roughly three seconds.
		constexpr double kHistoryRampPerSecond = 0.027;

		double s_HistoryHoldBeganAt = 0.0;
		double s_LastHistoryStepAt = 0.0;

		// Whether a held undo or redo is allowed to take another step this instant.
		bool HistoryStepIsDue(bool isRepeat, double now)
		{
			if (!isRepeat)
			{
				s_HistoryHoldBeganAt = now;
				s_LastHistoryStepAt = now;
				return true;
			}

			const double held = now - s_HistoryHoldBeganAt;

			if (held < kHistoryHoldDelay)
				return false;

			const double interval =
				std::max(kHistoryFastestInterval,
						 kHistoryFirstInterval - (held - kHistoryHoldDelay) * kHistoryRampPerSecond);

			if (now - s_LastHistoryStepAt < interval)
				return false;

			s_LastHistoryStepAt = now;
			return true;
		}

		int DigitCount(std::size_t value)
		{
			int digits = 1;

			while (value >= 10)
			{
				value /= 10;
				++digits;
			}

			return digits;
		}

		std::size_t AdvanceColumns(std::string_view expanded, std::size_t byte,
								   std::size_t columns)
		{
			std::size_t at = std::min(byte, expanded.size());

			for (std::size_t taken = 0; taken < columns && at < expanded.size(); ++taken)
				at = Core::NextCharacter(expanded, at);

			return at;
		}

		void DrawFailure(const Core::OpenItem& item)
		{
			const std::string message = Core::FailureMessage(item);
			const ImVec2 size = ImGui::CalcTextSize(message.c_str());
			const ImVec2 available = ImGui::GetContentRegionAvail();

			ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5f,
									   ImGui::GetCursorPosY() + (available.y - size.y) * 0.5f));
			ImGui::TextDisabled("%s", message.c_str());
		}
	}

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 float fontSize, Core::IndentStyle indentStyle,
								 bool wordWrap)
	{
		EditorFrameResult result;

		if (!item.Document)
		{
			DrawFailure(item);
			return result;
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), fontSize);

		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		// Read before anything in this function can move the caret — the mouse below
		// moves it as readily as the keyboard does, and the blink has to restart for
		// both.
		const std::size_t caretAtFrameStart = item.Caret.Offset;
		const float lineHeight = ImGui::GetTextLineHeight();
		// Every glyph in a monospaced face advances the same, so one glyph's advance is
		// the width of every cell.
		//
		// Taken from the baked font rather than from ImGui::CalcTextSize, which ends with
		// `text_size.x = ImCeilFast(text_size.x)` and so hands back the advance rounded up
		// to a whole pixel. ImDrawList::AddText advances by the true fractional value, so
		// a rounded cell width drifts by ceil(advance) - advance per column against the
		// glyphs it is supposed to line up with — several pixels by column 40, which is
		// enough to put the caret, the selection edges, the indent guides and the mouse
		// hit test each a character away from the text. GetCharAdvance is the same number
		// AddText walks by, unrounded.
		//
		// The baked size is whatever ImGui rounded the pushed size to, which is not
		// necessarily the size in effect (global and DPI scale factors are applied to
		// GetFontSize() but the bake is picked from the rounded result), so the advance is
		// scaled from the bake to the current size exactly as ImFont::RenderText does.
		//
		// Non-const because GetCharAdvance builds the glyph on demand if this size has not
		// seen it yet — this ImGui bakes lazily, so the first frame at a new zoom level is
		// the one that loads it.
		ImFontBaked* const baked = ImGui::GetFontBaked();
		const float cellWidth =
			baked->GetCharAdvance('0') * (ImGui::GetFontSize() / baked->Size);

		const int digits = std::max(kMinimumGutterDigits, DigitCount(buffer.GetLineCount()));
		const float breakpointColumnWidth = kBreakpointColumnCells * cellWidth;
		const float foldColumnWidth = kFoldColumnCells * cellWidth;
		const float numberColumnWidth =
			static_cast<float>(digits) * cellWidth + kGutterPaddingX * 2.0f;

		// [ breakpoint margin ][ line number ][ fold arrow ]  code
		//
		// Only the number column draws anything today; the other two are reserved so
		// the debugger and code folding — separate stages, still ahead — do not shift
		// every horizontal position (clicks, caret, selection, guides) the user has
		// already looked at when they arrive.
		const float gutterWidth = breakpointColumnWidth + numberColumnWidth + foldColumnWidth;

		// NoNavInputs matters as much as the scrollbar flag: with ImGui's keyboard
		// navigation on and no navigable item inside this child, the arrow keys, Page
		// Up/Down and Home/End would all additionally be consumed by ImGui's own
		// scroll-the-window fallback (Home/End jump straight to the document's start or
		// end via SetScrollY) and fight the caret-driven scrolling below in the same
		// frame. NoNavInputs makes those fallbacks return early while leaving window
		// focus and IsWindowFocused() working exactly as before.
		const ImGuiWindowFlags flags =
			ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs;

		if (ImGui::BeginChild("##EditorSurface", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, flags))
		{
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const float scrollX = ImGui::GetScrollX();
			const ImVec2 origin = ImGui::GetCursorScreenPos();

			// What is left for text once the gutter has taken its share, in whole columns
			// rather than pixels. The quantisation is not a bound on rebuilds — a cell is
			// about eight pixels, so dragging a window edge across a monitor still forces
			// a couple of hundred full rebuilds — it only takes the rebuild off the frames
			// where the width moved by less than a cell, which is most of them during a
			// slow drag. What makes that survivable is that a rebuild is linear in the
			// document; if it stops being, this is where it is felt. Never zero — a window
			// narrower than the gutter is a layout state, not an error, and WrapColumns
			// would stand still on a width of zero.
			const float available = ImGui::GetContentRegionAvail().x;
			const std::size_t columnsPerRow =
				wordWrap ? static_cast<std::size_t>(
							   std::max(1.0f, (available - gutterWidth) / cellWidth))
						 : Core::RowMap::kNoWrap;

			// The line-count test is not redundant with the width test: Editing keeps the
			// map current through every edit, but a document could reach here without ever
			// having passed through RefreshDerived, and a map with more lines than the
			// buffer would send RowAt off the end of it.
			if (columnsPerRow != item.WrappedTo || item.Rows.GetLineCount() != buffer.GetLineCount())
			{
				item.Rows.Rebuild(buffer, columnsPerRow, Core::DefaultTabWidth);
				item.WrappedTo = columnsPerRow;

				// The aim was a column of the geometry that just went away: after a resize
				// or a toggle of wrapping, the caret sits at a different row column than
				// the one it was aiming at. Cleared rather than converted, so the next
				// vertical move resolves it from where the caret actually is now.
				item.RowAimAt = Core::NoRowAim;
			}

			// Computed once when the document loaded (OpenItem::widestColumn), not walked
			// here every frame — that would cost a std::string allocation per line,
			// exactly the per-frame cost the clipper below exists to avoid.
			//
			// With wrapping on nothing is ever drawn past the right edge, so the window's
			// own width is the extent and widestColumn says nothing about it.
			const float contentWidth =
				wordWrap ? available
						 : gutterWidth + static_cast<float>(item.WidestColumn + 1) * cellWidth;

			// Pre-edit: this frame's mouse and keyboard handling, further down, can
			// still grow or shrink the document before the frame is over. Kept under
			// this name only for the interactive rectangle below, which is allowed to
			// hit-test against a size one frame stale — the reservation the scrollbars
			// and the clipper actually read is computed fresh, after that handling runs,
			// immediately before the clipper starts (see currentDocumentHeight below).
			const float documentHeight = static_cast<float>(item.Rows.TotalRows()) * lineHeight;

			// The one text origin in the file: everything below that positions against
			// the text — clicks, caret, selection, guides — measures from this rather
			// than recomputing origin.x + gutterWidth a second time.
			const float textOriginX = origin.x + gutterWidth;
			// Rows, not lines: Page Up and Page Down move by what fits on screen, and what
			// fits on screen is rows once a line can occupy several of them.
			const std::size_t pageRows = static_cast<std::size_t>(
				std::max(1.0f, ImGui::GetWindowHeight() / lineHeight));

			// Mouse handling rides ImGui's own button latch (see the comment on
			// IsItemActive below) rather than a hand-rolled one. An ImGui::InvisibleButton
			// would be the obvious way to get that, but InvisibleButton calls ItemSize on
			// its own bounding box, which pushes DC.CursorMaxPos out to match — and the
			// interactive area has to be at least as big as the visible window (a click
			// below a short file's last line, or right of its longest one, must still
			// land), which is routinely bigger than the document. Sized that way through
			// InvisibleButton, ContentSize would grow to fill the window on every ordinary
			// file, and with this child's zero window padding that makes the scrollbar
			// condition `ContentSize > Size - ScrollbarSize` true unconditionally — a
			// horizontal scrollbar on every file whose longest line actually fits, and the
			// same for the vertical one on any file shorter than the pane.
			//
			// So this reproduces InvisibleButton's ItemAdd + ButtonBehavior pair by hand
			// (imgui_widgets.cpp's own InvisibleButton, minus its ItemSize call) instead:
			// the interactive rectangle can be exactly as large as it needs to be for
			// hit-testing without ever being mistaken for the document's own size, because
			// nothing here calls ItemSize on it. ItemAdd and ButtonBehavior both key off
			// g.LastItemData.ID / g.ActiveId, not off anything ItemSize would have set, so
			// IsItemActivated() and IsItemActive() below work exactly as they would after
			// a real InvisibleButton.
			const ImVec2 interactiveExtent(std::max(contentWidth, ImGui::GetWindowWidth()),
										   std::max(documentHeight, ImGui::GetWindowHeight()));
			const ImRect interactiveBounds(
				origin, ImVec2(origin.x + interactiveExtent.x, origin.y + interactiveExtent.y));
			const ImGuiID interactiveId = ImGui::GetID("##EditorInput");

			ImGui::ItemAdd(interactiveBounds, interactiveId, nullptr, ImGuiItemFlags_NoNav);
			ImGui::ButtonBehavior(interactiveBounds, interactiveId, nullptr, nullptr);

			const bool pressedThisFrame = ImGui::IsItemActivated();
			const bool held = ImGui::IsItemActive();

			// No explicit focus call is needed here: ButtonBehavior (imgui_widgets.cpp,
			// around line 630) calls FocusWindow(window) itself the moment a press claims
			// ActiveId, with `window` being the current window at that point in the code —
			// this child, not its parent tab — which is exactly what makes the arrow keys
			// reach this document rather than the dockspace after a click.

			// Only a frame that actually presses the button computes a click, and only a
			// frame where the mouse has moved since the press computes a drag — never a
			// frame where the button is merely still held with the mouse motionless.
			// Without that second condition, every frame between a double or triple
			// click's press and its release (a click spans several frames at 60 Hz) would
			// fall into the drag branch below and overwrite the word or line SelectWordAt
			// / SelectLineAt just selected with PlaceAt(..., true, ...) at the still
			// stationary mouse position, collapsing the selection to nothing before the
			// button is ever released.
			const bool dragging = held && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

			if (pressedThisFrame || dragging)
			{
				const ImVec2 mouse = ImGui::GetMousePos();

				// Both axes are clamped to zero *before* the cast, not after. The
				// subtractions are float arithmetic and go negative the moment the mouse is
				// above the text origin or left of it — dragging up out of the window, or
				// sideways across the gutter — and converting a negative float to
				// std::size_t gives an enormous number rather than a small one. As a row
				// that reads as "past the end of the document"; as a column it is worse
				// still, because VisualToCaret adds the row's own start column to it, which
				// wraps around and clamps to some arbitrary column mid-row instead of the
				// row's beginning.
				const auto row =
					static_cast<std::size_t>(std::max(0.0f, (mouse.y - origin.y) / lineHeight));

				// The half cell is the rounding this has always used: a click past a cell's
				// middle belongs to the boundary on its right, not to the one on its left.
				const float relativeX = std::max(0.0f, mouse.x - textOriginX);
				const auto column =
					static_cast<std::size_t>((relativeX + cellWidth * 0.5f) / cellWidth);

				// `column` is measured from the row's left edge, which on a continuation
				// row is partway into the line — VisualToCaret is the one place that knows
				// how to put a row and a column within it back together into an offset, and
				// it clamps a column past the row's end rather than failing, which is what
				// makes a click in the empty space right of a short row land on its end.
				const std::size_t offset = Core::VisualToCaret(
					buffer, item.Rows, Core::VisualPosition{row, column},
					Core::DefaultTabWidth);

				if (pressedThisFrame)
				{
					const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);

					// A triple click selects the whole line, not the row it landed on: a
					// row is a consequence of the window's width, and the user asked for
					// the line.
					if (clicks >= 3)
						item.Caret = Core::SelectLineAt(buffer, item.Rows.RowAt(row).Line);
					else if (clicks == 2)
						item.Caret = Core::SelectWordAt(buffer, offset);
					else
						item.Caret = Core::PlaceAt(buffer, item.Caret, offset,
												   ImGui::GetIO().KeyShift,
												   Core::DefaultTabWidth);
				}
				else
				{
					// Dragging always extends, whichever way it goes.
					item.Caret = Core::PlaceAt(buffer, item.Caret, offset, true,
											   Core::DefaultTabWidth);
				}
			}

			// Fired here rather than reported upwards: this window is what defines the
			// scope, and the value used to travel three levels — widget,
			// DocumentWindows::Render's sum, its return value — before anything acted on
			// it. Which modifiers count and how a fast spin's several notches turn into
			// several steps are the router's and the keymap's business now, so neither
			// KeyCtrl nor the wheel value is inspected here.
			// All three checked, not just the router: the struct's members each default to
			// nullptr, so a partly filled one is a thing the type admits, and a guard that
			// reads as "this may arrive incomplete" while crashing if it does is worse
			// than no guard at all.
			const bool routable =
				context.Router != nullptr && context.Commands != nullptr && context.Keys != nullptr;

			// The wheel stays on the draw-time path rather than moving to events with the
			// keyboard: it is a gesture the user aims, and which editor it was aimed at is
			// known here and nowhere else.
			if (ImGui::IsWindowHovered() && routable)
			{
				const Core::MouseScrolledEvent scrolled{ImGui::GetIO().MouseWheelH,
														ImGui::GetIO().MouseWheel};

				context.Router->OnMouseScrolled(*context.Commands, *context.Keys,
												Core::Chord::Scope::HoveredEditor, scrolled);
			}

			// The keyboard is scoped to this document's own child window, not merely to
			// the parent tab: IsWindowFocused() with no flags checks the *current* window
			// (the child pushed by BeginChild above), and only the document the user
			// actually clicked into ever becomes that window. Without this, a key press
			// reaching any visible editor — including one in another split — could move a
			// caret the user is not looking at.
			//
			// The caret is drawn using this same test further down, rather than a
			// separately computed one: the keyboard and the blinking caret must never
			// disagree about which editor is active, and asking IsWindowFocused() twice
			// (once here, once below) risks exactly that if ImGui's focus state ever
			// changes between the two calls in the same frame.
			const bool focused = ImGui::IsWindowFocused();

			// The queue is what the keyboard used to be polled for. A context arriving
			// without one is a caller that has no input to offer, not an error: this
			// document then simply draws, exactly as an unfocused one does.
			if (focused && context.Input != nullptr)
			{
				const Core::FrameInput& input = *context.Input;

				// Asked of the stroke rather than of the keyboard: the queue is drained
				// after the fact, and by then the modifiers held are not necessarily the
				// ones that were held when the key went down.
				const auto shiftOf = [](const Core::KeyStroke& stroke) {
					return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Shift);
				};

				// On Windows, AltGr reports as Ctrl+Alt, not as a key of its own — so
				// typing a Polish letter that needs AltGr (ą, ć, ę, ...) would otherwise
				// read as Ctrl+<letter> here. Excluding Alt keeps every shortcut in
				// this block, not just Ctrl+A and Ctrl+C, from firing on those letters.
				const auto controlOf = [](const Core::KeyStroke& stroke) {
					return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Control) &&
						   !Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Alt);
				};

				struct Binding
				{
					Core::KeyCode Key;
					Core::Motion Plain;
					Core::Motion WithControl;
				};

				// Horizontal motion only. Up, Down, Page Up, Page Down and the plain Home
				// and End move by screen row instead, below: that is what the user sees,
				// and Core's Move cannot do it — it knows nothing about the row map, which
				// only the widget owns. Ctrl+Home and Ctrl+End stay here, since the start
				// and the end of the document are the same offsets however the text is laid
				// out.
				const Binding bindings[] = {
					{Core::KeyCode::Left, Core::Motion::Left, Core::Motion::WordLeft},
					{Core::KeyCode::Right, Core::Motion::Right, Core::Motion::WordRight},
				};

				// Where the caret sits on screen. Computed on demand rather than once per
				// frame: it copies the caret's line, which on a minified file is the whole
				// file, and only these motions need it.
				const auto caretRow = [&] {
					return Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset,
											   Core::DefaultTabWidth)
						.Row;
				};

				// Puts the caret at a column of a row, and is the only writer of the aim.
				// `aiming` is what separates a vertical move — which keeps aiming at the
				// column it started from, so a short row passed on the way does not pull it
				// left for good — from Home and End, which stop aiming: the column they
				// land on is the caret's own row column, which is what the next vertical
				// move resolves for itself anyway.
				//
				// `shift` is passed in rather than captured: it belongs to the stroke being
				// handled, and the strokes of one frame need not have been held alike.
				const auto placeOnRow = [&](std::size_t targetRow, std::size_t column,
											bool aiming, bool shift) {
					const std::size_t offset = Core::VisualToCaret(
						buffer, item.Rows, Core::VisualPosition{targetRow, column},
						Core::DefaultTabWidth);

					item.Caret.Offset = offset;

					// Shift extends, which is the only thing PlaceAt would have done for us
					// here; its offset snapping and its column are both wrong for this
					// path — VisualToCaret already lands on a character boundary, and the
					// column it writes is a column within the line, not within the row.
					if (!shift)
						item.Caret.Anchor = offset;

					if (aiming)
					{
						item.RowAim = column;
						item.RowAimAt = offset;
					}
					else
					{
						item.RowAimAt = Core::NoRowAim;
					}
				};

				const auto moveByRows = [&](bool down, std::size_t rows, bool shift) {
					const Core::VisualPosition here = Core::CaretToVisual(
						buffer, item.Rows, item.Caret.Offset, Core::DefaultTabWidth);

					// The aim is trusted only while the caret is still where it was
					// computed for. Anything else that moved the caret — a click, an arrow
					// key, an edit, a rebuild at a new width — leaves the two disagreeing,
					// and the aim resolves from the caret's current row column instead.
					// Order-independent by construction: it does not matter whether that
					// other move happened earlier in this frame or ten frames ago.
					const std::size_t aim =
						item.RowAimAt == item.Caret.Offset ? item.RowAim : here.Column;

					// A target past the last row resolves to the last row (RowAt clamps),
					// which is the same standing-still that moving down on the last line
					// always did.
					const std::size_t target =
						down ? here.Row + rows : (here.Row >= rows ? here.Row - rows : 0);

					placeOnRow(target, aim, true, shift);
				};

				// Movement first, in its own pass over the queue, and the edits below in a
				// second one after the undo group has been broken and the typed characters
				// inserted. That is the order this ran in when every key was polled — all the
				// motions, then the text, then the rest — and the break between the two passes
				// is what makes moving and then typing a fresh undo step.
				//
				// Auto-repeat is accepted here throughout: holding an arrow, Page Up or Home
				// moves for as long as it is held. Only Ctrl+A refuses a repeat, being a
				// selection rather than a motion.
				for (const Core::KeyStroke& stroke : input.GetStrokes())
				{
					const bool shift = shiftOf(stroke);
					const bool control = controlOf(stroke);

					for (const Binding& binding : bindings)
					{
						if (stroke.Key != binding.Key)
							continue;

						item.Caret =
							Core::Move(buffer, item.Caret,
									   control ? binding.WithControl : binding.Plain, shift,
									   Core::DefaultTabWidth, pageRows);
					}

					if (stroke.Key == Core::KeyCode::Up)
						moveByRows(false, 1, shift);

					if (stroke.Key == Core::KeyCode::Down)
						moveByRows(true, 1, shift);

					if (stroke.Key == Core::KeyCode::PageUp)
						moveByRows(false, pageRows, shift);

					if (stroke.Key == Core::KeyCode::PageDown)
						moveByRows(true, pageRows, shift);

					if (stroke.Key == Core::KeyCode::Home)
					{
						if (control)
						{
							item.Caret = Core::Move(buffer, item.Caret,
													Core::Motion::DocumentStart, shift,
													Core::DefaultTabWidth, pageRows);
						}
						else
						{
							placeOnRow(caretRow(), 0, false, shift);
						}
					}

					if (stroke.Key == Core::KeyCode::End)
					{
						if (control)
						{
							item.Caret = Core::Move(buffer, item.Caret,
													Core::Motion::DocumentEnd, shift,
													Core::DefaultTabWidth, pageRows);
						}
						else
						{
							// A column no row can reach, left to VisualToCaret's clamp — which
							// is precisely why it clamps rather than failing. On a wrapped row
							// that lands one column short of the right edge: the offset at the
							// edge is also the offset at the next row's column zero, and the
							// rule assigns it to the next row. Deliberate, and recorded as
							// debt — reaching the edge needs a caret affinity bit.
							//
							// Halved rather than the maximum itself: VisualToCaret adds the
							// row's own start column to this before clamping, and that sum has
							// to stay representable.
							placeOnRow(caretRow(),
									   std::numeric_limits<std::size_t>::max() / 2, false,
									   shift);
						}
					}

					if (control && !stroke.IsRepeat && stroke.Key == Core::KeyCode::A)
						item.Caret = Core::SelectAll(buffer);
				}

				// Whatever moved the caret above — a motion key or Ctrl+A, and the mouse
				// click or drag earlier in this function, since caretAtFrameStart was
				// read before either ran — is a different gesture from whatever typing
				// follows. Breaking the group here, before any of the edits below,
				// means moving the caret and then typing costs a fresh undo step instead
				// of merging with whatever was typed before the caret moved.
				if (item.Caret.Offset != caretAtFrameStart)
					item.Document->GetBuffer().BreakUndoGroup();

				// Cutting and copying share this: cutting is copying followed by
				// removing the selection, not a separate way of reading the clipboard.
				const auto copySelection = [&] {
					const auto [from, to] = Core::SelectionRange(item.Caret);

					// Substring hands back the buffer's own LF-only text, and the Windows
					// clipboard is CRLF — pasted into Excel or any Win32 EDIT control,
					// LF-only text arrives as a single line.
					if (from != to)
					{
						const std::string clipboard = Core::WithLineEnding(
							buffer.Substring(from, to), Core::LineEnding::Crlf);

						ImGui::SetClipboardText(clipboard.c_str());
					}
				};

				// The characters before the keys below, which is the order this has always
				// run in: Enter, Tab and Backspace never reach the character queue — the
				// window layer reports them as keys and nothing else — so nothing here
				// double-fires.

				// A codepoint per character, after the layout and any dead key have had
				// their say. InsertText's contract is UTF-8, so each one is encoded before
				// reaching it. Skipping everything below 0x20 except tab, and 0x7f, is what
				// keeps a control character from ever being inserted literally.
				for (const unsigned int codepoint : input.GetCharacters())
				{
					if ((codepoint < 0x20 && codepoint != '\t') || codepoint == 0x7f)
						continue;

					char utf8[5];
					const int length = ImTextCharToUtf8(utf8, codepoint);

					if (length > 0)
					{
						result.Edited |= Core::InsertText(
							item, std::string_view(utf8, static_cast<std::size_t>(length)));
					}
				}

				for (const Core::KeyStroke& stroke : input.GetStrokes())
				{
					const bool shift = shiftOf(stroke);
					const bool control = controlOf(stroke);

					if (stroke.Key == Core::KeyCode::Enter || stroke.Key == Core::KeyCode::KPEnter)
					{
						result.Edited |= Core::InsertNewline(item, indentStyle);
					}

					if (stroke.Key == Core::KeyCode::Backspace)
					{
						result.Edited |= control ? Core::DeleteWordBackward(item)
												 : Core::DeleteBackward(item);
					}

					if (stroke.Key == Core::KeyCode::Delete)
					{
						result.Edited |= control ? Core::DeleteWordForward(item)
												 : Core::DeleteForward(item);
					}

					// Consumed here rather than left for ImGui's own focus navigation, which
					// is exactly what NoNavInputs on this child window (see its own comment
					// above) makes safe — with the nav fallbacks disabled, nothing else is
					// listening for Tab.
					if (stroke.Key == Core::KeyCode::Tab)
					{
						result.Edited |= shift ? Core::Outdent(item, indentStyle)
											   : Core::Indent(item, indentStyle);
					}

					const bool undoPressed = control && !shift && stroke.Key == Core::KeyCode::Z;
					const bool redoPressed =
						control && (stroke.Key == Core::KeyCode::Y ||
									(shift && stroke.Key == Core::KeyCode::Z));

					// Asked once for the pair, so holding one and rolling onto the other does
					// not restart the ramp mid-gesture.
					if ((undoPressed || redoPressed) &&
						HistoryStepIsDue(stroke.IsRepeat, ImGui::GetTime()))
					{
						result.Edited |= undoPressed ? Core::Undo(item) : Core::Redo(item);
					}

					// What follows are commands rather than edits, and holding one of them
					// would repeat an action nobody asked to repeat — a held Ctrl+V pasting
					// the clipboard over and over — so they answer to a first press only.
					if (stroke.IsRepeat)
						continue;

					if (control && stroke.Key == Core::KeyCode::V)
					{
						// An empty clipboard, or one holding something that is not text,
						// hands back a null pointer rather than an empty string.
						if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr)
							result.Edited |= Core::InsertText(item, clipboard);
					}

					if (control && stroke.Key == Core::KeyCode::X)
					{
						copySelection();
						result.Edited |= Core::DeleteBackward(item);
					}

					// Saving is not this widget's decision: a stale document on disk needs
					// confirming first, and only DocumentWindows knows how to ask that. This
					// only reports that the user asked. !shift, the same as Ctrl+Z two lines
					// above: without it Ctrl+Shift+S also reads as a request to save this one
					// document, colliding with DocumentWindows's own Ctrl+Shift+S handling
					// for "save every dirty document" and writing this one twice from a
					// single keypress.
					if (control && !shift && stroke.Key == Core::KeyCode::S)
						result.SaveRequested = true;

					if (control && stroke.Key == Core::KeyCode::C)
						copySelection();
				}
			}

			// Scrolling follows the caret only when this frame actually moved it — not
			// merely when the window has keyboard focus, which is true on every frame it
			// is focused and would fight the user's own scrolling. Comparing the caret's
			// offset against caretAtFrameStart, read before the mouse and keyboard above
			// touched anything, is the honest signal: true exactly when a click, a drag,
			// a motion key or an edit changed where the caret sits.
			if (item.Caret.Offset != caretAtFrameStart)
			{
				const Core::Position where = buffer.ToPosition(item.Caret.Offset);
				// The row the caret is on, which is its line only while nothing wraps.
				// Scrolling to the line number in a wrapped document would land wherever
				// that many rows happen to reach, which is short of the caret by however
				// much the text above it wrapped.
				const float caretY =
					static_cast<float>(Core::CaretToVisual(buffer, item.Rows,
														   item.Caret.Offset,
														   Core::DefaultTabWidth)
										   .Row) *
					lineHeight;

				if (caretY < ImGui::GetScrollY())
					ImGui::SetScrollY(caretY);
				else if (caretY + lineHeight > ImGui::GetScrollY() + ImGui::GetWindowHeight())
					ImGui::SetScrollY(caretY + lineHeight - ImGui::GetWindowHeight());

				// The same rule sideways, which End on a long line and Ctrl+Right through
				// one both need. `caretX` is measured in content coordinates, where the
				// gutter occupies the first gutterWidth — but the gutter is painted over
				// the window's left edge rather than scrolling with the content, so the
				// leftmost text a reader can actually see sits at scrollX + gutterWidth,
				// and bringing the caret to the left edge means scrolling to
				// caretX - gutterWidth, not to caretX.
				//
				// Skipped entirely while wrapping: there is no horizontal range to scroll
				// through, and chasing the caret across one that is zero wide would fight
				// the vertical scroll above on every frame the caret moves.
				if (!wordWrap)
				{
					const float caretX =
						gutterWidth + static_cast<float>(Core::ColumnOfByte(
										  buffer.LineText(where.Line), where.Column,
										  Core::DefaultTabWidth)) *
										  cellWidth;

					if (caretX - gutterWidth < ImGui::GetScrollX())
						ImGui::SetScrollX(caretX - gutterWidth);
					else if (caretX + cellWidth > ImGui::GetScrollX() + ImGui::GetWindowWidth())
						ImGui::SetScrollX(caretX + cellWidth - ImGui::GetWindowWidth());
				}
			}

			// Whatever moved it — a key above, a click or a drag earlier in this
			// function — the blink starts over, so the caret is lit the moment it lands.
			if (item.Caret.Offset != caretAtFrameStart)
				s_CaretMovedAt = ImGui::GetTime();

			const Core::Position caretPosition =
				buffer.ToPosition(item.Caret.Offset);
			// Where the caret is on screen, which is (line, column) only while nothing
			// wraps. Read once here rather than per row: the loop below only compares
			// against it, and CaretToVisual walks the caret line's break list.
			const Core::VisualPosition caretVisual = Core::CaretToVisual(
				buffer, item.Rows, item.Caret.Offset, Core::DefaultTabWidth);
			const auto [selectionFrom, selectionTo] = Core::SelectionRange(item.Caret);

			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);
			const ImU32 gutterColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const ImU32 selectionColour = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
			const ImU32 currentLineColour = ImGui::GetColorU32(ImGuiCol_FrameBg, 0.45f);
			// Structure, not content: clearly dimmer than the line numbers, which already
			// use TextDisabled at full strength.
			const ImU32 guideColour = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.35f);

			// The number is right-aligned within its own column, which sits between the
			// two reserved margins — but the opaque fill below covers the whole gutter,
			// margins included: the gutter does not scroll while the guides, selection,
			// current-line highlight and glyphs behind it do, so anything short of the
			// full width would leave scrolled content showing through the margins.
			const float numberColumnX0 = origin.x + scrollX + breakpointColumnWidth;
			const float numberColumnX1 = numberColumnX0 + numberColumnWidth;

			// Reserved from the document as it stands now — after this frame's mouse and
			// keyboard handling above, not from documentHeight/contentWidth captured
			// before it (those exist only for the interactive rectangle's hit-testing,
			// where a frame of lag is imperceptible). Reserving from the pre-edit extent
			// here instead left the clipper below walking one line past what Dummy had
			// reserved the instant Enter grew the document — SetCursorPosY landing past
			// CursorMaxPos is exactly what EndChild's assertion tests for. The same
			// mismatch ran unnoticed the other way (a deleted line leaving the
			// reservation one too tall for a frame) and on the other axis (typing into
			// the longest line leaving the width one cell narrow), neither of which
			// asserts because neither moves the cursor — but it is the same defect, so
			// both the height and the width read post-edit state here.
			//
			// This Dummy is the *only* thing in this function allowed to grow
			// ContentSize — see the interactive rectangle above, which deliberately
			// avoids doing the same — and SetCursorScreenPos puts the cursor straight
			// back at origin, which is where the clipper's rows below assume it starts.
			//
			// Rows, not lines: a wrapped line reserves as many of them as it occupies,
			// and with wrapping off the map gives one row per line, so this is the same
			// number it always was.
			const float currentDocumentHeight =
				static_cast<float>(item.Rows.TotalRows()) * lineHeight;
			const float currentContentWidth =
				wordWrap ? available
						 : gutterWidth + static_cast<float>(item.WidestColumn + 1) * cellWidth;

			ImGui::Dummy(ImVec2(currentContentWidth, currentDocumentHeight));
			ImGui::SetCursorScreenPos(origin);

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(item.Rows.TotalRows()), lineHeight);

			// A wrapped line owns a run of consecutive rows, and the clipper walks rows in
			// ascending order, so the line's text and its tab expansion are fetched once
			// for that whole run rather than once per row. Without this a minified file —
			// one line of several megabytes — would be copied and expanded once for every
			// row of itself on screen, which the clipper does nothing to bound.
			std::string text;
			std::string expanded;
			std::size_t cachedLine = 0;
			bool haveCachedLine = false;

			// The selection's span within the cached line, in columns. Cached for the same
			// reason and at the same moment: both ends are properties of the line, not of
			// the row, and ColumnOfByte walks the line from byte 0. Computed per row they
			// cost two walks of the whole line per visible row per frame — on a minified
			// file, megabytes sixty times over, for a number that does not change.
			std::size_t selectionFromColumn = 0;
			std::size_t selectionToColumn = 0;

			// The byte the previous row ended at, and the row that follows it. Valid only
			// for that one row: the clipper may jump, and a jump lands somewhere the carry
			// says nothing about.
			std::size_t carriedByte = 0;
			std::size_t carriedRow = 0;
			bool haveCarried = false;

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::size_t rowIndex = static_cast<std::size_t>(row);
					const Core::RowRef ref = item.Rows.RowAt(rowIndex);
					const std::size_t line = ref.Line;
					const float y = origin.y + static_cast<float>(row) * lineHeight;
					const float textX = textOriginX;

					if (!haveCachedLine || cachedLine != line)
					{
						text = buffer.LineText(line);
						// Tabs are advances, not glyphs; expanding them into spaces first
						// keeps a row one AddText call instead of one run per tab.
						expanded = Core::ExpandTabs(text, Core::DefaultTabWidth);
						cachedLine = line;
						haveCachedLine = true;
						haveCarried = false;

						const std::size_t start = buffer.LineStart(line);

						selectionFromColumn = 0;
						selectionToColumn = 0;

						if (selectionFrom != selectionTo && selectionTo > start && selectionFrom <= start + text.size())
						{
							const std::size_t fromByte =
								selectionFrom > start ? selectionFrom - start : 0;
							const std::size_t toByte =
								std::min(selectionTo - start, text.size() + 1);

							selectionFromColumn = Core::ColumnOfByte(
								text, fromByte, Core::DefaultTabWidth);
							// A selection running past this line's end shows the newline
							// as one cell of highlight, which is how every editor draws
							// it — that is the trailing +1.
							selectionToColumn =
								Core::ColumnOfByte(text, std::min(toByte, text.size()),
												   Core::DefaultTabWidth) +
								(toByte > text.size() ? 1 : 0);
						}
					}

					const std::size_t lineStart = buffer.LineStart(line);

					// The slice of the line this row shows, as a half-open column range.
					// A break column is where the *next* row starts, so it is this row's
					// end and the following row's beginning. With wrapping off there are
					// no breaks and every row is the whole line.
					//
					// Guarded rather than indexed straight: `RowsOf(line) ==
					// BreaksOf(line).size() + 1` holds everywhere today, but
					// RowMap::SetRowsOf forces a count without touching the breaks and so
					// can decouple the two. Folding, in stage 5, is what will call it — a
					// line set to zero rows would send this read past the end. Falling
					// back to the line's own first column draws the wrong slice rather
					// than reading memory that is not there.
					const std::vector<std::size_t>& breaks = item.Rows.BreaksOf(line);
					const std::size_t fromColumn =
						ref.SubRow == 0 || ref.SubRow > breaks.size() ? 0
																	  : breaks[ref.SubRow - 1];
					const bool lastRowOfLine = ref.SubRow >= breaks.size();

					// Sliced by converting columns to bytes rather than by indexing the
					// expanded string with columns: the expansion makes one column one
					// *cell*, not one byte, and a line holding anything outside ASCII
					// would be cut mid-character by the second reading.
					const std::size_t rowFromByte = haveCarried && carriedRow == rowIndex
														? carriedByte
														: AdvanceColumns(expanded, 0, fromColumn);
					const std::size_t rowToByte =
						lastRowOfLine ? expanded.size()
									  : AdvanceColumns(expanded, rowFromByte,
													   breaks[ref.SubRow] - fromColumn);

					carriedByte = rowToByte;
					carriedRow = rowIndex + 1;
					haveCarried = true;

					if (line == caretPosition.Line)
					{
						// currentContentWidth, not contentWidth: this highlight is drawn
						// from the same post-edit state the clipper itself is walking,
						// so a line that just became the widest one gets the full-width
						// highlight this frame rather than one frame later.
						draw->AddRectFilled(
							ImVec2(textX, y),
							ImVec2(origin.x + std::max(currentContentWidth, ImGui::GetWindowWidth()),
								   y + lineHeight),
							currentLineColour);
					}

					// Under the text and the selection, so neither is ever obscured. One
					// segment per row rather than one long line per column: a guide only
					// exists on rows whose depth demands it, and a column's guide can stop
					// and restart down the file.
					const std::size_t depth =
						line < item.IndentDepths.size() ? item.IndentDepths[line] : 0;

					// Positioned relative to this row's first column, not to the line's:
					// a continuation row starts partway into the line, and a guide drawn
					// from the line's own column 0 would sit that far to the right of the
					// character it belongs under. A guide whose column is behind this
					// row's start belongs to a row above and is not drawn again here.
					for (std::size_t column = 0; column < depth;
						 column += Core::DefaultTabWidth)
					{
						if (column < fromColumn)
							continue;

						const float guideX =
							textX + static_cast<float>(column - fromColumn) * cellWidth;

						draw->AddLine(ImVec2(guideX, y), ImVec2(guideX, y + lineHeight),
									  guideColour, 1.0f);
					}

					if (selectionFrom != selectionTo && selectionTo > lineStart && selectionFrom <= lineStart + text.size())
					{
						// selectionFromColumn and selectionToColumn were measured once for
						// this line, in the caching block above, under this same condition.

						// Clipped to this row's own column span, so a selection crossing
						// several rows fills each one from its own left edge rather than
						// drawing the whole line's span on every row of it. The last row of
						// a line has no right bound: the newline cell sits past the line's
						// last column, and with wrapping off every row is the whole line.
						const std::size_t rowToColumn =
							lastRowOfLine ? std::numeric_limits<std::size_t>::max()
										  : breaks[ref.SubRow];
						const std::size_t clippedFrom =
							std::clamp(selectionFromColumn, fromColumn, rowToColumn);
						const std::size_t clippedTo =
							std::clamp(selectionToColumn, fromColumn, rowToColumn);

						if (clippedTo > clippedFrom)
						{
							// Measured from this row's first column, not from the line's:
							// textX is where this row starts drawing.
							const float fromX =
								textX + static_cast<float>(clippedFrom - fromColumn) * cellWidth;
							const float toX =
								textX + static_cast<float>(clippedTo - fromColumn) * cellWidth;

							draw->AddRectFilled(ImVec2(fromX, y), ImVec2(toX, y + lineHeight),
												selectionColour);
						}
					}

					if (rowFromByte < rowToByte)
					{
						draw->AddText(ImVec2(textX, y), textColour,
									  expanded.data() + rowFromByte,
									  expanded.data() + rowToByte);
					}

					// Drawn last and offset by the horizontal scroll, so the gutter stays
					// pinned to the left while the text slides under it. The fill spans
					// the whole gutter — both reserved margins included — so nothing
					// scrolled draws through where they sit; the number itself stays
					// right-aligned inside its own column.
					//
					// The number's width comes from cellWidth — every digit is one cell in
					// a monospaced face — and not from ImGui::CalcTextSize, which rounds up
					// to a whole pixel: the column this is aligned inside was itself sized
					// as digits * cellWidth, and two different measurements of one width
					// are exactly how the caret drifted off the text in the first place.
					//
					// The fill is drawn on every row — it is what stops scrolled content
					// showing through the gutter — but the number itself only on a line's
					// first row. A continuation row is the same line of the file, and
					// repeating its number would say the file has more lines than it does.
					draw->AddRectFilled(ImVec2(origin.x + scrollX, y),
										ImVec2(origin.x + scrollX + gutterWidth, y + lineHeight),
										ImGui::GetColorU32(ImGuiCol_WindowBg));

					if (ref.SubRow == 0)
					{
						const std::string number = std::to_string(line + 1);
						const float numberX = numberColumnX1 - kGutterPaddingX - static_cast<float>(number.size()) * cellWidth;

						draw->AddText(ImVec2(numberX, y),
									  line == caretPosition.Line ? textColour : gutterColour,
									  number.c_str());
					}

					// Only the focused editor blinks a caret: an unfocused pane still
					// shows its selection above (a user glancing at a split needs to see
					// what is selected there), but drawing a second blinking caret when
					// only one editor receives the keyboard is what this guards against.
					//
					// Tested against the caret's *row*, not its line: a wrapped line
					// occupies several rows and the caret is on exactly one of them, so
					// comparing lines would draw it once per row. caretVisual.Column is
					// already measured from this row's left edge, which is what makes this
					// the same arithmetic whether the line wrapped or not.
					if (focused && rowIndex == caretVisual.Row)
					{
						const float caretX =
							textX + static_cast<float>(caretVisual.Column) * cellWidth;

						// Measured from the last movement rather than from zero, which is
						// what keeps the caret from being invisible under a held arrow
						// key: every move restarts the cycle at its lit half.
						const double phase =
							std::fmod(ImGui::GetTime() - s_CaretMovedAt, kCaretBlinkPeriod);

						if (phase < kCaretBlinkPeriod * 0.5)
						{
							draw->AddLine(ImVec2(caretX, y), ImVec2(caretX, y + lineHeight),
										  textColour, 1.0f);
						}
					}
				}
			}

			clipper.End();
		}

		ImGui::EndChild();
		ImGui::PopFont();

		return result;
	}
}