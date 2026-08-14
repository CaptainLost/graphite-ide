#include "Editor/TextEditorWidget.h"

#include "Editor/EditorPalette.h"
#include "Editor/FindNavigation.h"
#include "Editor/MatchColumns.h"
#include "Editor/Palettes.h"

#include "Lang/Syntax/SyntaxCache.h"

#include "Theme/BuiltInSchemes.h"

#include <IconsMaterialDesignIcons.h>

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
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kGutterPaddingX = 8.0f;

		constexpr int kMinimumGutterDigits = 3;

		constexpr float kBreakpointColumnCells = 1.0f;

		constexpr float kFoldColumnCells = 1.0f;

		constexpr double kCaretBlinkPeriod = 1.0;

		// The find bar's own left and right inset. Its vertical padding is zero: the bar is one
		// frame tall, which is exactly what the widgets inside it need and nothing more.
		constexpr float kFindBarPaddingX = 6.0f;

		// What the bar keeps clear of the editor's top and right edges, and of the scrollbar when
		// one is showing. A floating box wedged into the corner reads as a rendering mistake.
		constexpr float kFindBarMargin = 6.0f;

		constexpr float kFindSeparatorWidth = 1.0f;

		// Handed to DrawRowText for a document whose language has no lexer, so the row loop
		// never has to branch on whether SyntaxCache exists — an empty token list already
		// draws the row in one colour. Function-local rather than a file scope object, per ADR
		// 0033 on complex initialization of globals.
		const std::vector<Token>& NoTokens()
		{
			static const std::vector<Token> tokens;

			return tokens;
		}

		double s_CaretMovedAt = 0.0;

		// Which document the blink above is being timed for. One value for the whole
		// application, like the clock itself: only the focused editor draws a caret, so only
		// one document is ever being timed.
		std::filesystem::path s_BlinkOwner;

		// The blink is measured from the last caret movement, and switching documents is not
		// one — so without this the caret in the newly chosen document inherits the phase of
		// the one just left and can spend its first half-second dark, exactly when the user
		// is looking for it. Keyed on the path rather than the item's address, which a
		// reallocation of the open-document list can hand to a different document.
		void RestartBlinkOnDocumentChange(const std::filesystem::path& path)
		{
			if (s_BlinkOwner == path)
				return;

			s_BlinkOwner = path;
			s_CaretMovedAt = ImGui::GetTime();
		}

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

		// [ breakpoint margin ][ line number ][ fold arrow ]  code
		//
		// Only the number column draws anything today; the other two are reserved so that the
		// debugger and code folding — separate stages, still ahead — do not shift every
		// horizontal position the user has already looked at when they arrive.
		struct GutterMetrics
		{
			float BreakpointWidth = 0.0f;
			float NumberWidth = 0.0f;
			float FoldWidth = 0.0f;
			float TotalWidth = 0.0f;
		};

		struct EditorMetrics
		{
			float LineHeight = 0.0f;
			float CellWidth = 0.0f;
			GutterMetrics Gutter;
		};

		float MeasureCellWidth()
		{
			// Taken from the baked font rather than from ImGui::CalcTextSize, which ends with
			// `text_size.x = ImCeilFast(text_size.x)` and so hands back the advance rounded up
			// to a whole pixel. ImDrawList::AddText advances by the true fractional value, so a
			// rounded cell width drifts by ceil(advance) - advance per column against the glyphs
			// it is supposed to line up with — several pixels by column 40, enough to put the
			// caret, the selection edges, the indent guides and the mouse hit test each a
			// character away from the text. GetCharAdvance is the same number AddText walks by,
			// unrounded.
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

			// Every glyph in a monospaced face advances the same, so one glyph's advance is the
			// width of every cell.
			return baked->GetCharAdvance('0') * (ImGui::GetFontSize() / baked->Size);
		}

		EditorMetrics MeasureEditor(const Core::TextBuffer& buffer)
		{
			EditorMetrics metrics;

			metrics.LineHeight = ImGui::GetTextLineHeight();
			metrics.CellWidth = MeasureCellWidth();

			const int digits = std::max(kMinimumGutterDigits, DigitCount(buffer.GetLineCount()));

			metrics.Gutter.BreakpointWidth = kBreakpointColumnCells * metrics.CellWidth;
			metrics.Gutter.FoldWidth = kFoldColumnCells * metrics.CellWidth;
			metrics.Gutter.NumberWidth =
				static_cast<float>(digits) * metrics.CellWidth + kGutterPaddingX * 2.0f;
			metrics.Gutter.TotalWidth = metrics.Gutter.BreakpointWidth + metrics.Gutter.NumberWidth +
										metrics.Gutter.FoldWidth;

			return metrics;
		}

		ImGuiWindowFlags EditorSurfaceFlags()
		{
			// NoNavInputs matters as much as the scrollbar flag: with ImGui's keyboard
			// navigation on and no navigable item inside this child, the arrow keys, Page
			// Up/Down and Home/End would all additionally be consumed by ImGui's own
			// scroll-the-window fallback (Home/End jump straight to the document's start or end
			// via SetScrollY) and fight the caret-driven scrolling below in the same frame.
			// NoNavInputs makes those fallbacks return early while leaving window focus and
			// IsWindowFocused() working exactly as before.
			return ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs;
		}

		std::size_t ColumnsThatFit(const EditorMetrics& metrics, float available, bool wordWrap)
		{
			// What is left for text once the gutter has taken its share, in whole columns rather
			// than pixels. The quantisation is not a bound on rebuilds — a cell is about eight
			// pixels, so dragging a window edge across a monitor still forces a couple of hundred
			// full rebuilds — it only takes the rebuild off the frames where the width moved by
			// less than a cell, which is most of them during a slow drag. What makes that
			// survivable is that a rebuild is linear in the document; if it stops being, this is
			// where it is felt. Never zero — a window narrower than the gutter is a layout state,
			// not an error, and WrapColumns would stand still on a width of zero.
			if (!wordWrap)
				return Core::RowMap::kNoWrap;

			return static_cast<std::size_t>(
				std::max(1.0f, (available - metrics.Gutter.TotalWidth) / metrics.CellWidth));
		}

		void RewrapIfGeometryChanged(Core::OpenItem& item, const Core::TextBuffer& buffer,
									 std::size_t columnsPerRow)
		{
			// The line-count test is not redundant with the width test: Editing keeps the map
			// current through every edit, but a document could reach here without ever having
			// passed through RefreshDerived, and a map with more lines than the buffer would send
			// RowAt off the end of it.
			if (columnsPerRow == item.WrappedTo && item.Rows.GetLineCount() == buffer.GetLineCount())
				return;

			item.Rows.Rebuild(buffer, columnsPerRow, item.TabWidth);
			item.WrappedTo = columnsPerRow;

			// The aim was a column of the geometry that just went away: after a resize or a
			// toggle of wrapping, the caret sits at a different row column than the one it was
			// aiming at. Cleared rather than converted, so the next vertical move resolves it
			// from where the caret actually is now.
			item.RowAimAt = Core::NoRowAim;
		}

		// WidestColumn is computed once when the document loaded, not walked here every frame —
		// that would cost a std::string allocation per line, exactly the per-frame cost the
		// clipper exists to avoid. With wrapping on nothing is ever drawn past the right edge, so
		// the window's own width is the extent and WidestColumn says nothing about it.
		float ContentWidthOf(const Core::OpenItem& item, const EditorMetrics& metrics,
							 float available, bool wordWrap)
		{
			if (wordWrap)
				return available;

			return metrics.Gutter.TotalWidth +
				   static_cast<float>(item.WidestColumn + 1) * metrics.CellWidth;
		}

		// Rows, not lines: a wrapped line occupies as many of them as it needs, and with wrapping
		// off the map gives one row per line.
		float DocumentHeightOf(const Core::OpenItem& item, const EditorMetrics& metrics)
		{
			return static_cast<float>(item.Rows.TotalRows()) * metrics.LineHeight;
		}

		// Rows, not lines: Page Up and Page Down move by what fits on screen, and what fits on
		// screen is rows once a line can occupy several of them.
		std::size_t RowsPerPage(const EditorMetrics& metrics)
		{
			return static_cast<std::size_t>(
				std::max(1.0f, ImGui::GetWindowHeight() / metrics.LineHeight));
		}

		struct PointerState
		{
			bool PressedThisFrame = false;
			bool Dragging = false;
		};

		PointerState AddInteractiveSurface(const ImVec2& origin, float contentWidth,
										   float documentHeight)
		{
			// Mouse handling rides ImGui's own button latch rather than a hand-rolled one. An
			// ImGui::InvisibleButton would be the obvious way to get that, but InvisibleButton
			// calls ItemSize on its own bounding box, which pushes DC.CursorMaxPos out to match —
			// and the interactive area has to be at least as big as the visible window (a click
			// below a short file's last line, or right of its longest one, must still land),
			// which is routinely bigger than the document. Sized that way through
			// InvisibleButton, ContentSize would grow to fill the window on every ordinary file,
			// and with this child's zero window padding that makes the scrollbar condition
			// `ContentSize > Size - ScrollbarSize` true unconditionally — a horizontal scrollbar
			// on every file whose longest line actually fits, and the same for the vertical one
			// on any file shorter than the pane.
			//
			// So this reproduces InvisibleButton's ItemAdd + ButtonBehavior pair by hand
			// (imgui_widgets.cpp's own InvisibleButton, minus its ItemSize call) instead: the
			// interactive rectangle can be exactly as large as it needs to be for hit-testing
			// without ever being mistaken for the document's own size, because nothing here calls
			// ItemSize on it. ItemAdd and ButtonBehavior both key off g.LastItemData.ID /
			// g.ActiveId, not off anything ItemSize would have set, so IsItemActivated() and
			// IsItemActive() work exactly as they would after a real InvisibleButton.
			const ImVec2 extent(std::max(contentWidth, ImGui::GetWindowWidth()),
								std::max(documentHeight, ImGui::GetWindowHeight()));
			const ImRect bounds(origin, ImVec2(origin.x + extent.x, origin.y + extent.y));
			const ImGuiID id = ImGui::GetID("##EditorInput");

			ImGui::ItemAdd(bounds, id, nullptr, ImGuiItemFlags_NoNav);
			ImGui::ButtonBehavior(bounds, id, nullptr, nullptr);

			// No explicit focus call is needed here: ButtonBehavior (imgui_widgets.cpp, around
			// line 630) calls FocusWindow(window) itself the moment a press claims ActiveId, with
			// `window` being the current window at that point in the code — this child, not its
			// parent tab — which is exactly what makes the arrow keys reach this document rather
			// than the dockspace after a click.
			PointerState pointer;

			pointer.PressedThisFrame = ImGui::IsItemActivated();

			// Only a frame where the mouse has moved since the press counts as a drag, never a
			// frame where the button is merely still held with the mouse motionless. Without that
			// second condition, every frame between a double or triple click's press and its
			// release (a click spans several frames at 60 Hz) would fall into the drag branch and
			// overwrite the word or line SelectWordAt / SelectLineAt just selected with
			// PlaceAt(..., true, ...) at the still stationary mouse position, collapsing the
			// selection to nothing before the button is ever released.
			pointer.Dragging =
				ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

			return pointer;
		}

		void ApplyMouseToCaret(const PointerState& pointer, const Core::TextBuffer& buffer,
							   Core::OpenItem& item, const EditorMetrics& metrics,
							   const ImVec2& origin, float textOriginX)
		{
			if (!pointer.PressedThisFrame && !pointer.Dragging)
				return;

			const ImVec2 mouse = ImGui::GetMousePos();

			// Both axes are clamped to zero *before* the cast, not after. The subtractions are
			// float arithmetic and go negative the moment the mouse is above the text origin or
			// left of it — dragging up out of the window, or sideways across the gutter — and
			// converting a negative float to std::size_t gives an enormous number rather than a
			// small one. As a row that reads as "past the end of the document"; as a column it is
			// worse still, because VisualToCaret adds the row's own start column to it, which
			// wraps around and clamps to some arbitrary column mid-row instead of the row's
			// beginning.
			const auto row = static_cast<std::size_t>(
				std::max(0.0f, (mouse.y - origin.y) / metrics.LineHeight));

			// The half cell is the rounding this has always used: a click past a cell's middle
			// belongs to the boundary on its right, not to the one on its left.
			const float relativeX = std::max(0.0f, mouse.x - textOriginX);
			const auto column = static_cast<std::size_t>((relativeX + metrics.CellWidth * 0.5f) /
														 metrics.CellWidth);

			// `column` is measured from the row's left edge, which on a continuation row is
			// partway into the line — VisualToCaret is the one place that knows how to put a row
			// and a column within it back together into an offset, and it clamps a column past
			// the row's end rather than failing, which is what makes a click in the empty space
			// right of a short row land on its end.
			const std::size_t offset =
				Core::VisualToCaret(buffer, item.Rows, Core::VisualPosition{row, column},
									item.TabWidth);

			if (!pointer.PressedThisFrame)
			{
				// Dragging always extends, whichever way it goes.
				item.Caret = Core::PlaceAt(buffer, item.Caret, offset, true, item.TabWidth);
				return;
			}

			const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);

			// A triple click selects the whole line, not the row it landed on: a row is a
			// consequence of the window's width, and the user asked for the line.
			if (clicks >= 3)
				item.Caret = Core::SelectLineAt(buffer, item.Rows.RowAt(row).Line);
			else if (clicks == 2)
				item.Caret = Core::SelectWordAt(buffer, offset);
			else
				item.Caret = Core::PlaceAt(buffer, item.Caret, offset, ImGui::GetIO().KeyShift,
										   item.TabWidth);
		}

		void RouteMouseWheel(const CommandContext& context)
		{
			// All three checked, not just the router: the struct's members each default to
			// nullptr, so a partly filled one is a thing the type admits, and a guard that reads
			// as "this may arrive incomplete" while crashing if it does is worse than no guard at
			// all.
			const bool routable = context.Router != nullptr && context.Commands != nullptr &&
								  context.Keys != nullptr;

			// The wheel stays on the draw-time path rather than moving to events with the
			// keyboard: it is a gesture the user aims, and which editor it was aimed at is known
			// here and nowhere else. Which modifiers count and how a fast spin's several notches
			// turn into several steps are the router's and the keymap's business, so neither
			// KeyCtrl nor the wheel value is inspected here.
			if (!ImGui::IsWindowHovered() || !routable)
				return;

			const Core::MouseScrolledEvent scrolled{ImGui::GetIO().MouseWheelH,
													ImGui::GetIO().MouseWheel};

			context.Router->OnMouseScrolled(*context.Commands, *context.Keys,
											Core::Chord::Scope::HoveredEditor, scrolled);
		}

		// Asked of the stroke rather than of the keyboard: the queue is drained after the fact,
		// and by then the modifiers held are not necessarily the ones that were held when the key
		// went down.
		bool ShiftOf(const Core::KeyStroke& stroke)
		{
			return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Shift);
		}

		// On Windows, AltGr reports as Ctrl+Alt, not as a key of its own — so typing a Polish
		// letter that needs AltGr (ą, ć, ę, ...) would otherwise read as Ctrl+<letter>. Excluding
		// Alt keeps every shortcut in this file, not just Ctrl+A and Ctrl+C, from firing on those
		// letters.
		bool ControlOf(const Core::KeyStroke& stroke)
		{
			return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Control) &&
				   !Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Alt);
		}

		// Where the caret sits on screen. Computed on demand rather than once per frame: it
		// copies the caret's line, which on a minified file is the whole file, and only the row
		// motions need it.
		std::size_t RowOfCaret(const Core::TextBuffer& buffer, const Core::OpenItem& item)
		{
			return Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth)
				.Row;
		}

		// Puts the caret at a column of a row, and is the only writer of the aim. `aiming` is what
		// separates a vertical move — which keeps aiming at the column it started from, so a short
		// row passed on the way does not pull it left for good — from Home and End, which stop
		// aiming: the column they land on is the caret's own row column, which is what the next
		// vertical move resolves for itself anyway.
		//
		// `shift` is passed in rather than read from the keyboard: it belongs to the stroke being
		// handled, and the strokes of one frame need not have been held alike.
		void PlaceOnRow(const Core::TextBuffer& buffer, Core::OpenItem& item, std::size_t targetRow,
						std::size_t column, bool aiming, bool shift)
		{
			const std::size_t offset =
				Core::VisualToCaret(buffer, item.Rows, Core::VisualPosition{targetRow, column},
									item.TabWidth);

			item.Caret.Offset = offset;

			// Shift extends, which is the only thing PlaceAt would have done for us here; its
			// offset snapping and its column are both wrong for this path — VisualToCaret already
			// lands on a character boundary, and the column it writes is a column within the
			// line, not within the row.
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
		}

		void MoveByRows(const Core::TextBuffer& buffer, Core::OpenItem& item, bool down,
						std::size_t rows, bool shift)
		{
			const Core::VisualPosition here =
				Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth);

			// The aim is trusted only while the caret is still where it was computed for.
			// Anything else that moved the caret — a click, an arrow key, an edit, a rebuild at a
			// new width — leaves the two disagreeing, and the aim resolves from the caret's
			// current row column instead. Order-independent by construction: it does not matter
			// whether that other move happened earlier in this frame or ten frames ago.
			const std::size_t aim = item.RowAimAt == item.Caret.Offset ? item.RowAim : here.Column;

			// A target past the last row resolves to the last row (RowAt clamps), which is the
			// same standing-still that moving down on the last line always did.
			const std::size_t target =
				down ? here.Row + rows : (here.Row >= rows ? here.Row - rows : 0);

			PlaceOnRow(buffer, item, target, aim, true, shift);
		}

		void MoveToRowStart(const Core::TextBuffer& buffer, Core::OpenItem& item, bool shift)
		{
			PlaceOnRow(buffer, item, RowOfCaret(buffer, item), 0, false, shift);
		}

		void MoveToRowEnd(const Core::TextBuffer& buffer, Core::OpenItem& item, bool shift)
		{
			// A column no row can reach, left to VisualToCaret's clamp — which is precisely why
			// it clamps rather than failing. On a wrapped row that lands one column short of the
			// right edge: the offset at the edge is also the offset at the next row's column
			// zero, and the rule assigns it to the next row. Deliberate, and recorded as debt —
			// reaching the edge needs a caret affinity bit.
			//
			// Halved rather than the maximum itself: VisualToCaret adds the row's own start
			// column to this before clamping, and that sum has to stay representable.
			PlaceOnRow(buffer, item, RowOfCaret(buffer, item),
					   std::numeric_limits<std::size_t>::max() / 2, false, shift);
		}

		void ApplyMotionKeys(const Core::FrameInput& input, const Core::TextBuffer& buffer,
							 Core::OpenItem& item, std::size_t pageRows)
		{
			struct Binding
			{
				Core::KeyCode Key;
				Core::Motion Plain;
				Core::Motion WithControl;
			};

			// Horizontal motion only. Up, Down, Page Up, Page Down and the plain Home and End
			// move by screen row instead, below: that is what the user sees, and Core's Move
			// cannot do it — it knows nothing about the row map, which only the widget owns.
			// Ctrl+Home and Ctrl+End stay here, since the start and the end of the document are
			// the same offsets however the text is laid out.
			const Binding bindings[] = {
				{Core::KeyCode::Left, Core::Motion::Left, Core::Motion::WordLeft},
				{Core::KeyCode::Right, Core::Motion::Right, Core::Motion::WordRight},
			};

			// Auto-repeat is accepted here throughout: holding an arrow, Page Up or Home moves
			// for as long as it is held. Only Ctrl+A refuses a repeat, being a selection rather
			// than a motion.
			for (const Core::KeyStroke& stroke : input.GetStrokes())
			{
				const bool shift = ShiftOf(stroke);
				const bool control = ControlOf(stroke);

				for (const Binding& binding : bindings)
				{
					if (stroke.Key != binding.Key)
						continue;

					item.Caret = Core::Move(buffer, item.Caret,
											control ? binding.WithControl : binding.Plain, shift,
											item.TabWidth, pageRows);
				}

				if (stroke.Key == Core::KeyCode::Up)
					MoveByRows(buffer, item, false, 1, shift);

				if (stroke.Key == Core::KeyCode::Down)
					MoveByRows(buffer, item, true, 1, shift);

				if (stroke.Key == Core::KeyCode::PageUp)
					MoveByRows(buffer, item, false, pageRows, shift);

				if (stroke.Key == Core::KeyCode::PageDown)
					MoveByRows(buffer, item, true, pageRows, shift);

				if (stroke.Key == Core::KeyCode::Home)
				{
					if (control)
					{
						item.Caret = Core::Move(buffer, item.Caret, Core::Motion::DocumentStart,
												shift, item.TabWidth, pageRows);
					}
					else
					{
						MoveToRowStart(buffer, item, shift);
					}
				}

				if (stroke.Key == Core::KeyCode::End)
				{
					if (control)
					{
						item.Caret = Core::Move(buffer, item.Caret, Core::Motion::DocumentEnd,
												shift, item.TabWidth, pageRows);
					}
					else
					{
						MoveToRowEnd(buffer, item, shift);
					}
				}

				if (control && !stroke.IsRepeat && stroke.Key == Core::KeyCode::A)
					item.Caret = Core::SelectAll(buffer);
			}
		}

		// Cutting and copying share this: cutting is copying followed by removing the selection,
		// not a separate way of reading the clipboard.
		void CopySelectionToClipboard(const Core::TextBuffer& buffer, const Core::Caret& caret)
		{
			const auto [from, to] = Core::SelectionRange(caret);

			if (from == to)
				return;

			// Substring hands back the buffer's own LF-only text, and the Windows clipboard is
			// CRLF — pasted into Excel or any Win32 EDIT control, LF-only text arrives as a
			// single line.
			const std::string clipboard =
				Core::WithLineEnding(buffer.Substring(from, to), Core::LineEnding::Crlf);

			ImGui::SetClipboardText(clipboard.c_str());
		}

		// Called after every mutation that actually changed the buffer, never on one that
		// merely tried to — an Invalidate on a no-op edit would arm SyntaxCache's revision
		// backstop and leave it primed to swallow the next genuine edit that forgets to call
		// this.
		//
		// Invalidated from the topmost line the edit touched, not simply from where the caret
		// ended up. The caret is only one END of a selection — the anchor can sit above it —
		// so the line the edit actually BEGAN on is the selection's start, not the caret's
		// line: with the anchor above the caret, replacing a multi-line selection with text
		// that grows the line count leaves both the pre-edit and the post-edit caret lines
		// below where the edit started, and invalidating from either of those would keep stale
		// entry states for the lines in between. `Invalidate(n)` keeps the entry states for
		// lines 0..n and drops the rest, so `lineBefore` has to be the line the edit started
		// on; the lower of it and the post-edit caret line is the one every case agrees was
		// affected.
		void InvalidateSyntaxAfterEdit(SyntaxCache* syntax, const Core::TextBuffer& buffer,
									   const Core::OpenItem& item, std::size_t lineBefore,
									   bool changed)
		{
			if (!changed || syntax == nullptr)
			{
				return;
			}

			const std::size_t lineAfter = buffer.ToPosition(item.Caret.Offset).Line;

			syntax->Invalidate(std::min(lineBefore, lineAfter));
		}

		bool InsertTypedCharacters(const Core::FrameInput& input, Core::OpenItem& item,
								   SyntaxCache* syntax)
		{
			bool edited = false;

			const Core::TextBuffer& buffer = item.Document->GetBuffer();

			// A codepoint per character, after the layout and any dead key have had their say.
			// InsertText's contract is UTF-8, so each one is encoded before reaching it. Skipping
			// everything below 0x20 except tab, and 0x7f, is what keeps a control character from
			// ever being inserted literally. Enter, Tab and Backspace never reach this queue —
			// the window layer reports them as keys and nothing else — so nothing double-fires.
			for (const unsigned int codepoint : input.GetCharacters())
			{
				if ((codepoint < 0x20 && codepoint != '\t') || codepoint == 0x7f)
					continue;

				char utf8[5];
				const int length = ImTextCharToUtf8(utf8, codepoint);

				if (length > 0)
				{
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed = Core::InsertText(
						item, std::string_view(utf8, static_cast<std::size_t>(length)));

					edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}
			}

			return edited;
		}

		void ApplyEditKeys(const Core::FrameInput& input, const Core::TextBuffer& buffer,
						   Core::OpenItem& item, Core::IndentStyle indentStyle,
						   SyntaxCache* syntax, EditorFrameResult& result)
		{
			for (const Core::KeyStroke& stroke : input.GetStrokes())
			{
				const bool shift = ShiftOf(stroke);
				const bool control = ControlOf(stroke);

				if (stroke.Key == Core::KeyCode::Enter || stroke.Key == Core::KeyCode::KPEnter)
				{
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed = Core::InsertNewline(item, indentStyle);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}

				if (stroke.Key == Core::KeyCode::Backspace)
				{
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed =
						control ? Core::DeleteWordBackward(item) : Core::DeleteBackward(item);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}

				if (stroke.Key == Core::KeyCode::Delete)
				{
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed =
						control ? Core::DeleteWordForward(item) : Core::DeleteForward(item);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}

				// Consumed here rather than left for ImGui's own focus navigation, which is
				// exactly what NoNavInputs on this child window makes safe — with the nav
				// fallbacks disabled, nothing else is listening for a bare Tab.
				//
				// Ctrl+Tab is the exception: NoNavInputs does not reach it, because ImGui
				// routes the window switcher globally rather than through the focused window.
				// Indenting on it as well would mean one keystroke both opening the switcher
				// and quietly editing the document behind it.
				if (!control && stroke.Key == Core::KeyCode::Tab)
				{
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed =
						shift ? Core::Outdent(item, indentStyle) : Core::Indent(item, indentStyle);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}

				const bool undoPressed = control && !shift && stroke.Key == Core::KeyCode::Z;
				const bool redoPressed = control && (stroke.Key == Core::KeyCode::Y ||
													 (shift && stroke.Key == Core::KeyCode::Z));

				// Asked once for the pair, so holding one and rolling onto the other does not
				// restart the ramp mid-gesture.
				if ((undoPressed || redoPressed) &&
					HistoryStepIsDue(stroke.IsRepeat, ImGui::GetTime()))
				{
					// The caret cannot say what an undo or redo group touched: an undone group
					// leaves no selection to read the start from, and a redo of a multi-edit
					// group can leave the caret at the group's LAST edit while its first sat
					// higher up the file. Rather than track the group's own span, this
					// invalidates from line 0 unconditionally — a full re-lex is cheap next to
					// colouring the file wrongly until the next edit, and undo/redo already
					// breaks the undo group, so it is rare enough that the blunt instrument
					// costs nothing worth optimising away.
					const bool changed = undoPressed ? Core::Undo(item) : Core::Redo(item);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, 0, changed);
				}

				// What follows are commands rather than edits, and holding one of them would
				// repeat an action nobody asked to repeat — a held Ctrl+V pasting the clipboard
				// over and over — so they answer to a first press only.
				if (stroke.IsRepeat)
					continue;

				if (control && stroke.Key == Core::KeyCode::V)
				{
					// An empty clipboard, or one holding something that is not text, hands back a
					// null pointer rather than an empty string.
					if (const char* clipboard = ImGui::GetClipboardText(); clipboard != nullptr)
					{
						const std::size_t lineBefore =
							buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
						const bool changed = Core::InsertText(item, clipboard);
						result.Edited |= changed;
						InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
					}
				}

				// Only with something selected. DeleteBackward on an empty selection removes the
				// character before the caret, so a stray Ctrl+X used to eat a character and put
				// nothing on the clipboard — a deletion with no way to get the text back.
				if (control && stroke.Key == Core::KeyCode::X && Core::HasSelection(item.Caret))
				{
					CopySelectionToClipboard(buffer, item.Caret);
					const std::size_t lineBefore =
						buffer.ToPosition(Core::SelectionRange(item.Caret).first).Line;
					const bool changed = Core::DeleteBackward(item);
					result.Edited |= changed;
					InvalidateSyntaxAfterEdit(syntax, buffer, item, lineBefore, changed);
				}

				// Saving is not this widget's decision: a stale document on disk needs confirming
				// first, and only DocumentWindows knows how to ask that. This only reports that
				// the user asked. !shift, the same as Ctrl+Z above: without it Ctrl+Shift+S also
				// reads as a request to save this one document, colliding with DocumentWindows's
				// own Ctrl+Shift+S handling for "save every dirty document" and writing this one
				// twice from a single keypress.
				if (control && !shift && stroke.Key == Core::KeyCode::S)
					result.SaveRequested = true;

				if (control && stroke.Key == Core::KeyCode::C)
					CopySelectionToClipboard(buffer, item.Caret);
			}
		}

		void ApplyKeyboard(const Core::FrameInput& input, const Core::TextBuffer& buffer,
						   Core::OpenItem& item, Core::IndentStyle indentStyle,
						   SyntaxCache* syntax, std::size_t pageRows,
						   std::size_t caretAtFrameStart, EditorFrameResult& result)
		{
			// Movement first, in its own pass over the queue, and the edits in a second one after
			// the undo group has been broken and the typed characters inserted. That is the order
			// this ran in when every key was polled — all the motions, then the text, then the
			// rest — and the break between the two passes is what makes moving and then typing a
			// fresh undo step.
			ApplyMotionKeys(input, buffer, item, pageRows);

			// Whatever moved the caret above — a motion key or Ctrl+A, and the mouse click or
			// drag earlier in the frame, since caretAtFrameStart was read before either ran — is
			// a different gesture from whatever typing follows. Breaking the group here, before
			// any of the edits below, means moving the caret and then typing costs a fresh undo
			// step instead of merging with whatever was typed before the caret moved.
			if (item.Caret.Offset != caretAtFrameStart)
				item.Document->GetBuffer().BreakUndoGroup();

			result.Edited |= InsertTypedCharacters(input, item, syntax);

			ApplyEditKeys(input, buffer, item, indentStyle, syntax, result);
		}

		void ScrollToCaret(const Core::TextBuffer& buffer, const Core::OpenItem& item,
						   const EditorMetrics& metrics, bool wordWrap)
		{
			const Core::Position where = buffer.ToPosition(item.Caret.Offset);

			// The row the caret is on, which is its line only while nothing wraps. Scrolling to
			// the line number in a wrapped document would land wherever that many rows happen to
			// reach, which is short of the caret by however much the text above it wrapped.
			const float caretY = static_cast<float>(Core::CaretToVisual(buffer, item.Rows,
																		item.Caret.Offset,
																		item.TabWidth)
														.Row) *
								 metrics.LineHeight;

			// No correction for the find bar in either edge. The bar is a compact overlay in the
			// top-right corner: it takes no room out of the content and covers only the tail of
			// the first rows, which is where a line has usually already ended. Biasing the scroll
			// for it would push the caret down a bar's height on every move, for the sake of a
			// column the text rarely reaches.
			if (caretY < ImGui::GetScrollY())
				ImGui::SetScrollY(caretY);
			else if (caretY + metrics.LineHeight > ImGui::GetScrollY() + ImGui::GetWindowHeight())
				ImGui::SetScrollY(caretY + metrics.LineHeight - ImGui::GetWindowHeight());

			// Skipped entirely while wrapping: there is no horizontal range to scroll through,
			// and chasing the caret across one that is zero wide would fight the vertical scroll
			// above on every frame the caret moves.
			if (wordWrap)
				return;

			// The same rule sideways, which End on a long line and Ctrl+Right through one both
			// need. `caretX` is measured in content coordinates, where the gutter occupies the
			// first gutterWidth — but the gutter is painted over the window's left edge rather
			// than scrolling with the content, so the leftmost text a reader can actually see
			// sits at scrollX + gutterWidth, and bringing the caret to the left edge means
			// scrolling to caretX - gutterWidth, not to caretX.
			const float caretX =
				metrics.Gutter.TotalWidth +
				static_cast<float>(Core::ColumnOfByte(buffer.LineText(where.Line), where.Column,
													  item.TabWidth)) *
					metrics.CellWidth;

			if (caretX - metrics.Gutter.TotalWidth < ImGui::GetScrollX())
				ImGui::SetScrollX(caretX - metrics.Gutter.TotalWidth);
			else if (caretX + metrics.CellWidth > ImGui::GetScrollX() + ImGui::GetWindowWidth())
				ImGui::SetScrollX(caretX + metrics.CellWidth - ImGui::GetWindowWidth());
		}

		// Which document's find field is waiting for the keyboard. Set when the bar is opened or
		// stepped from, consumed by the next frame that document draws its bar. Keyed on the
		// path rather than the item's address, for the same reason the blink owner above is: a
		// reallocation of the open-document list hands one address to a different document.
		std::filesystem::path s_FindFocusFor;

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

		void DrawMatchCounter(const Core::FindState& find, float width)
		{
			// Nothing drawn while the query is empty — "0 / 0" beside an empty field reports a
			// failure nobody has asked for yet — but the room stays claimed, so the bar is the
			// same width before the first keystroke as after it.
			if (find.Query.empty())
			{
				ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
				return;
			}

			const std::string counter = MatchCounterText(find);
			const float text = ImGui::CalcTextSize(counter.c_str()).x;

			// Right-aligned in its box, against the separator, so the digits grow leftwards into
			// the gap rather than pushing anything along.
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(width - text, 0.0f));

			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", counter.c_str());
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

		void DrawFindBar(Core::OpenItem& item, const Core::TextBuffer& buffer,
						 const FindBarFrame& frame)
		{
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

			// No conditional term anywhere in this sum: every part of it is a constant for a given
			// font, so the bar has one width for the whole time it is open and its left edge never
			// moves.
			const float barWidth =
				std::min(kFindBarPaddingX * 2.0f + FindFieldWidth() + style.ItemSpacing.x +
							 counterWidth + style.ItemSpacing.x + kFindSeparatorWidth +
							 (square + style.ItemSpacing.x) * 5.0f,
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

			if (s_FindFocusFor == item.Path)
			{
				// SetKeyboardFocusHere only takes effect on the very next widget submitted.
				ImGui::SetKeyboardFocusHere();
				s_FindFocusFor.clear();
			}

			if (noMatches)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, NoMatchColor());
			}

			// What is left after the controls, which is FindFieldWidth() at any width the bar was
			// not clamped at — and less than it when the panel is too narrow, so a cramped bar
			// gives up field rather than spilling its buttons past its own edge.
			const float controls = style.ItemSpacing.x + counterWidth + style.ItemSpacing.x +
								   kFindSeparatorWidth + (square + style.ItemSpacing.x) * 5.0f;

			ImGui::SetNextItemWidth(
				std::max(ImGui::GetContentRegionAvail().x - controls, ImGui::GetFontSize()));

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

			if (queryEdited)
			{
				item.Find.Query = field.data();
				RefreshFindMatches(item, buffer);
			}

			ImGui::SameLine();
			DrawMatchCounter(item.Find, counterWidth);

			// After the counter rather than before it: the field and the number of matches it
			// found are one thing, and the buttons that act on them are another.
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
				s_FindFocusFor = item.Path;
			}
		}

		// Everything a row needs that is the same for every row of the frame. ContentWidth is the
		// post-edit one, so a line that just became the widest gets its full-width highlight this
		// frame rather than one frame later.
		struct RowCanvas
		{
			ImDrawList* Draw = nullptr;
			ImVec2 Origin;
			float ScrollX = 0.0f;

			// The one text origin in the file: everything that positions against the text —
			// clicks, caret, selection, guides — measures from this.
			float TextOriginX = 0.0f;

			float ContentWidth = 0.0f;
			float WindowWidth = 0.0f;
			EditorMetrics Metrics;
			EditorPalette Palette;
			SyntaxPalette SyntaxColours;
		};

		// The slice of the line one row shows, as a half-open column range and the bytes of the
		// expanded text they correspond to.
		struct RowSlice
		{
			std::size_t Index = 0;
			std::size_t Line = 0;
			std::size_t SubRow = 0;
			float Y = 0.0f;
			std::size_t FromColumn = 0;
			std::size_t ToColumn = 0;
			bool LastRowOfLine = false;
			std::size_t FromByte = 0;
			std::size_t ToByte = 0;
		};

		// A wrapped line owns a run of consecutive rows, and the clipper walks rows in ascending
		// order, so the line's text, its tab expansion and the selection's span within it are
		// fetched once for that whole run rather than once per row. Without this a minified file —
		// one line of several megabytes — would be copied, expanded and walked twice by
		// ColumnOfByte for every row of itself on screen, which the clipper does nothing to bound.
		struct VisibleLineCache
		{
			std::string Text;
			std::string Expanded;
			std::size_t Line = 0;
			bool Valid = false;

			bool SelectionOnLine = false;
			std::size_t SelectionFromColumn = 0;
			std::size_t SelectionToColumn = 0;

			// Column ranges of every find match on this line, converted in CacheLine for the same
			// reason the selection is: the editor draws expanded text, and a raw byte offset
			// lands several cells out on any line holding a tab.
			LineMatches Matches;

			// The byte the previous row ended at, and the row that follows it. Valid only for
			// that one row: the clipper may jump, and a jump lands somewhere the carry says
			// nothing about.
			std::size_t CarriedByte = 0;
			std::size_t CarriedRow = 0;
			bool HaveCarried = false;
		};

		// Everything CacheLine needs that is the same for every line of the frame, bundled rather
		// than passed one argument at a time: the selection alone already put two std::size_t
		// side by side in the parameter list, where a caller can transpose them and the compiler
		// has nothing to say about it. Fields are filled by name at the one call site.
		struct LineCacheInput
		{
			const Core::TextBuffer& Buffer;

			// Document byte ranges, ascending, and which of them the caret is on.
			const std::vector<std::pair<std::size_t, std::size_t>>& Matches;

			std::pair<std::size_t, std::size_t> Selection;
			std::size_t CurrentMatch = 0;
			std::size_t TabWidth = Core::DefaultTabWidth;
		};

		void CacheLine(VisibleLineCache& cache, const LineCacheInput& input, std::size_t line)
		{
			if (cache.Valid && cache.Line == line)
				return;

			const auto [selectionFrom, selectionTo] = input.Selection;
			const std::size_t tabWidth = input.TabWidth;

			cache.Text = input.Buffer.LineText(line);
			// Tabs are advances, not glyphs; expanding them into spaces first keeps a row one
			// AddText call instead of one run per tab.
			cache.Expanded = Core::ExpandTabs(cache.Text, tabWidth);
			cache.Line = line;
			cache.Valid = true;
			cache.HaveCarried = false;

			const std::size_t start = input.Buffer.LineStart(line);

			LineMatchInput matchInput{input.Matches};

			matchInput.LineText = cache.Text;
			matchInput.LineStart = start;
			matchInput.CurrentMatch = input.CurrentMatch;
			matchInput.TabWidth = tabWidth;

			cache.Matches = MatchColumnsOnLine(matchInput);

			cache.SelectionFromColumn = 0;
			cache.SelectionToColumn = 0;
			cache.SelectionOnLine = selectionFrom != selectionTo && selectionTo > start &&
									selectionFrom <= start + cache.Text.size();

			if (!cache.SelectionOnLine)
				return;

			const std::size_t fromByte = selectionFrom > start ? selectionFrom - start : 0;
			const std::size_t toByte = std::min(selectionTo - start, cache.Text.size() + 1);

			cache.SelectionFromColumn =
				Core::ColumnOfByte(cache.Text, fromByte, tabWidth);
			// A selection running past this line's end shows the newline as one cell of
			// highlight, which is how every editor draws it — that is the trailing +1.
			cache.SelectionToColumn = Core::ColumnOfByte(cache.Text,
														 std::min(toByte, cache.Text.size()),
														 tabWidth) +
									  (toByte > cache.Text.size() ? 1 : 0);
		}

		RowSlice SliceRow(VisibleLineCache& cache, const Core::RowMap& rows, Core::RowRef ref,
						  std::size_t rowIndex, float y)
		{
			RowSlice slice;

			slice.Index = rowIndex;
			slice.Line = ref.Line;
			slice.SubRow = ref.SubRow;
			slice.Y = y;

			// A break column is where the *next* row starts, so it is this row's end and the
			// following row's beginning. With wrapping off there are no breaks and every row is
			// the whole line.
			//
			// Guarded rather than indexed straight: `RowsOf(line) == BreaksOf(line).size() + 1`
			// holds everywhere today, but RowMap::SetRowsOf forces a count without touching the
			// breaks and so can decouple the two. Folding, in stage 5, is what will call it — a
			// line set to zero rows would send this read past the end. Falling back to the line's
			// own first column draws the wrong slice rather than reading memory that is not
			// there.
			const std::vector<std::size_t>& breaks = rows.BreaksOf(ref.Line);

			slice.FromColumn =
				ref.SubRow == 0 || ref.SubRow > breaks.size() ? 0 : breaks[ref.SubRow - 1];
			slice.LastRowOfLine = ref.SubRow >= breaks.size();
			// The last row of a line has no right bound: the newline cell sits past the line's
			// last column, and with wrapping off every row is the whole line.
			slice.ToColumn = slice.LastRowOfLine ? std::numeric_limits<std::size_t>::max()
												 : breaks[ref.SubRow];

			// Sliced by converting columns to bytes rather than by indexing the expanded string
			// with columns: the expansion makes one column one *cell*, not one byte, and a line
			// holding anything outside ASCII would be cut mid-character by the second reading.
			slice.FromByte = cache.HaveCarried && cache.CarriedRow == rowIndex
								 ? cache.CarriedByte
								 : AdvanceColumns(cache.Expanded, 0, slice.FromColumn);
			slice.ToByte = slice.LastRowOfLine
							   ? cache.Expanded.size()
							   : AdvanceColumns(cache.Expanded, slice.FromByte,
												slice.ToColumn - slice.FromColumn);

			cache.CarriedByte = slice.ToByte;
			cache.CarriedRow = rowIndex + 1;
			cache.HaveCarried = true;

			return slice;
		}

		void DrawCurrentLineHighlight(const RowCanvas& canvas, const RowSlice& slice)
		{
			canvas.Draw->AddRectFilled(
				ImVec2(canvas.TextOriginX, slice.Y),
				ImVec2(canvas.Origin.x + std::max(canvas.ContentWidth, canvas.WindowWidth),
					   slice.Y + canvas.Metrics.LineHeight),
				canvas.Palette.CurrentLine);
		}

		// Drawn under the text and the selection, so neither is ever obscured. One segment per row
		// rather than one long line per column: a guide only exists on rows whose depth demands
		// it, and a column's guide can stop and restart down the file.
		//
		// Positioned relative to this row's first column, not to the line's: a continuation row
		// starts partway into the line, and a guide drawn from the line's own column 0 would sit
		// that far to the right of the character it belongs under. A guide whose column is behind
		// this row's start belongs to a row above and is not drawn again here.
		void DrawIndentGuides(const RowCanvas& canvas, const RowSlice& slice, std::size_t depth,
							  std::size_t tabWidth)
		{
			for (std::size_t column = 0; column < depth; column += tabWidth)
			{
				if (column < slice.FromColumn)
					continue;

				const float guideX =
					canvas.TextOriginX +
					static_cast<float>(column - slice.FromColumn) * canvas.Metrics.CellWidth;

				canvas.Draw->AddLine(ImVec2(guideX, slice.Y),
									 ImVec2(guideX, slice.Y + canvas.Metrics.LineHeight),
									 canvas.Palette.Guide, 1.0f);
			}
		}

		// Drawn before the selection and before the text, so a selected match still reads as
		// selected and the glyphs sit on top of both.
		void DrawRowMatches(const RowCanvas& canvas, const RowSlice& slice,
							const VisibleLineCache& cache)
		{
			for (std::size_t index = 0; index < cache.Matches.Columns.size(); ++index)
			{
				const MatchColumns& match = cache.Matches.Columns[index];

				const std::size_t clippedFrom =
					std::clamp(match.FromColumn, slice.FromColumn, slice.ToColumn);
				const std::size_t clippedTo =
					std::clamp(match.ToColumn, slice.FromColumn, slice.ToColumn);

				if (clippedTo <= clippedFrom)
				{
					continue;
				}

				const float fromX =
					canvas.TextOriginX +
					static_cast<float>(clippedFrom - slice.FromColumn) * canvas.Metrics.CellWidth;
				const float toX =
					canvas.TextOriginX +
					static_cast<float>(clippedTo - slice.FromColumn) * canvas.Metrics.CellWidth;

				const ImU32 color = index == cache.Matches.Current
										? canvas.Palette.FindCurrentMatch
										: canvas.Palette.FindMatch;

				canvas.Draw->AddRectFilled(ImVec2(fromX, slice.Y),
										   ImVec2(toX, slice.Y + canvas.Metrics.LineHeight),
										   color);
			}
		}

		// Clipped to this row's own column span, so a selection crossing several rows fills each
		// one from its own left edge rather than drawing the whole line's span on every row of it.
		void DrawRowSelection(const RowCanvas& canvas, const RowSlice& slice,
							  const VisibleLineCache& cache)
		{
			const std::size_t clippedFrom =
				std::clamp(cache.SelectionFromColumn, slice.FromColumn, slice.ToColumn);
			const std::size_t clippedTo =
				std::clamp(cache.SelectionToColumn, slice.FromColumn, slice.ToColumn);

			if (clippedTo <= clippedFrom)
				return;

			const float fromX =
				canvas.TextOriginX +
				static_cast<float>(clippedFrom - slice.FromColumn) * canvas.Metrics.CellWidth;
			const float toX =
				canvas.TextOriginX +
				static_cast<float>(clippedTo - slice.FromColumn) * canvas.Metrics.CellWidth;

			canvas.Draw->AddRectFilled(ImVec2(fromX, slice.Y),
									   ImVec2(toX, slice.Y + canvas.Metrics.LineHeight),
									   canvas.Palette.Selection);
		}

		// One AddText per token run rather than one for the row: the row is still a single
		// string, so the runs are slices of it and nothing is copied.
		//
		// Positioned by column, not by byte: the text has had its tabs expanded, but a column is
		// still one *cell* and a UTF-8 character is one to four bytes, so the x of a run is
		// counted in cells from the row's first column with ColumnCount.
		void DrawRowText(const RowCanvas& canvas, const RowSlice& slice,
						 const VisibleLineCache& cache, const std::vector<Token>& tokens,
						 std::size_t tabWidth)
		{
			if (slice.FromByte >= slice.ToByte)
			{
				return;
			}

			const char* const base = cache.Expanded.data();

			if (tokens.empty())
			{
				canvas.Draw->AddText(ImVec2(canvas.TextOriginX, slice.Y), canvas.Palette.Text,
									 base + slice.FromByte, base + slice.ToByte);
				return;
			}

			std::size_t at = slice.FromByte;
			std::size_t column = slice.FromColumn;

			const auto draw = [&](std::size_t to, ImU32 colour)
			{
				if (to <= at)
				{
					return;
				}

				const std::string_view run(base + at, to - at);
				const float x = canvas.TextOriginX + static_cast<float>(column - slice.FromColumn) *
													 canvas.Metrics.CellWidth;

				canvas.Draw->AddText(ImVec2(x, slice.Y), colour, run.data(),
									 run.data() + run.size());

				// tabWidth is inert here: run slices cache.Expanded, which has already had its
				// tabs expanded away, so ColumnCount cannot see one to size.
				column += Core::ColumnCount(run, tabWidth);
				at = to;
			};

			for (const Token& token : tokens)
			{
				if (token.To <= slice.FromByte)
				{
					continue;
				}

				if (token.From >= slice.ToByte)
				{
					break;
				}

				// The gap before this token is whitespace; drawing it keeps `column` advancing
				// without special-casing it.
				draw(std::max(token.From, slice.FromByte), canvas.Palette.Text);
				draw(std::min(token.To, slice.ToByte), canvas.SyntaxColours.Of(token.Kind));
			}

			draw(slice.ToByte, canvas.Palette.Text);
		}

		// Drawn last and offset by the horizontal scroll, so the gutter stays pinned to the left
		// while the text slides under it. The fill spans the whole gutter — both reserved margins
		// included — so nothing scrolled draws through where they sit; the number itself stays
		// right-aligned inside its own column.
		//
		// The number's width comes from cellWidth — every digit is one cell in a monospaced face
		// — and not from ImGui::CalcTextSize, which rounds up to a whole pixel: the column this is
		// aligned inside was itself sized as digits * cellWidth, and two different measurements of
		// one width are exactly how the caret drifted off the text in the first place.
		//
		// The fill is drawn on every row, but the number itself only on a line's first row. A
		// continuation row is the same line of the file, and repeating its number would say the
		// file has more lines than it does.
		void DrawGutter(const RowCanvas& canvas, const RowSlice& slice, bool onCaretLine)
		{
			const float gutterX = canvas.Origin.x + canvas.ScrollX;

			canvas.Draw->AddRectFilled(
				ImVec2(gutterX, slice.Y),
				ImVec2(gutterX + canvas.Metrics.Gutter.TotalWidth,
					   slice.Y + canvas.Metrics.LineHeight),
				canvas.Palette.GutterFill);

			if (slice.SubRow != 0)
				return;

			const std::string number = std::to_string(slice.Line + 1);
			const float numberColumnRight = gutterX + canvas.Metrics.Gutter.BreakpointWidth +
											canvas.Metrics.Gutter.NumberWidth;
			const float numberX = numberColumnRight - kGutterPaddingX -
								  static_cast<float>(number.size()) * canvas.Metrics.CellWidth;

			canvas.Draw->AddText(ImVec2(numberX, slice.Y),
								 onCaretLine ? canvas.Palette.Text : canvas.Palette.Number,
								 number.c_str());
		}

		void DrawCaret(const RowCanvas& canvas, const RowSlice& slice, std::size_t column)
		{
			const float caretX =
				canvas.TextOriginX + static_cast<float>(column) * canvas.Metrics.CellWidth;

			// Measured from the last movement rather than from zero, which is what keeps the
			// caret from being invisible under a held arrow key: every move restarts the cycle at
			// its lit half.
			const double phase = std::fmod(ImGui::GetTime() - s_CaretMovedAt, kCaretBlinkPeriod);

			if (phase < kCaretBlinkPeriod * 0.5)
			{
				canvas.Draw->AddLine(ImVec2(caretX, slice.Y),
									 ImVec2(caretX, slice.Y + canvas.Metrics.LineHeight),
									 canvas.Palette.Caret, 1.0f);
			}
		}

		struct CaretPaint
		{
			std::size_t Line = 0;

			// Where the caret is on screen, which is (line, column) only while nothing wraps.
			Core::VisualPosition Visual;

			// The same focus test the keyboard above was gated on, never a second one: an
			// unfocused pane still shows its selection — a user glancing at a split needs to see
			// what is selected there — but only the editor receiving the keyboard blinks a caret.
			bool Focused = false;
		};

		void DrawRow(const RowCanvas& canvas, const Core::OpenItem& item,
					 const VisibleLineCache& cache, const RowSlice& slice,
					 const std::vector<Token>& tokens, const CaretPaint& caret)
		{
			if (slice.Line == caret.Line)
				DrawCurrentLineHighlight(canvas, slice);

			const std::size_t depth =
				slice.Line < item.IndentDepths.size() ? item.IndentDepths[slice.Line] : 0;

			DrawIndentGuides(canvas, slice, depth, item.TabWidth);

			DrawRowMatches(canvas, slice, cache);

			if (cache.SelectionOnLine)
				DrawRowSelection(canvas, slice, cache);

			DrawRowText(canvas, slice, cache, tokens, item.TabWidth);
			DrawGutter(canvas, slice, slice.Line == caret.Line);

			// Tested against the caret's *row*, not its line: a wrapped line occupies several rows
			// and the caret is on exactly one of them, so comparing lines would draw it once per
			// row. The column is already measured from this row's left edge, which is what makes
			// this the same arithmetic whether the line wrapped or not.
			if (caret.Focused && slice.Index == caret.Visual.Row)
				DrawCaret(canvas, slice, caret.Visual.Column);
		}

		void ReserveDocumentExtent(const ImVec2& origin, float contentWidth, float documentHeight)
		{
			// Reserved from the document as it stands now — after this frame's mouse and keyboard
			// handling, not from the extent captured before it (that one exists only for the
			// interactive rectangle's hit-testing, where a frame of lag is imperceptible).
			// Reserving from the pre-edit extent here instead left the clipper below walking one
			// line past what Dummy had reserved the instant Enter grew the document —
			// SetCursorPosY landing past CursorMaxPos is exactly what EndChild's assertion tests
			// for. The same mismatch ran unnoticed the other way (a deleted line leaving the
			// reservation one too tall for a frame) and on the other axis (typing into the
			// longest line leaving the width one cell narrow), neither of which asserts because
			// neither moves the cursor — but it is the same defect, so both the height and the
			// width read post-edit state here.
			//
			// This Dummy is the *only* thing in this file allowed to grow ContentSize — see
			// AddInteractiveSurface, which deliberately avoids doing the same — and
			// SetCursorScreenPos puts the cursor straight back at origin, which is where the
			// clipper's rows below assume it starts.
			ImGui::Dummy(ImVec2(contentWidth, documentHeight));
			ImGui::SetCursorScreenPos(origin);
		}

		void DrawVisibleRows(const Core::TextBuffer& buffer, const Core::OpenItem& item,
							 const EditorMetrics& metrics, const ImVec2& origin, float scrollX,
							 float available, bool wordWrap, bool focused, SyntaxCache* syntax,
							 const ResolvedScheme& scheme)
		{
			RowCanvas canvas;

			canvas.Draw = ImGui::GetWindowDrawList();
			canvas.Origin = origin;
			canvas.ScrollX = scrollX;
			canvas.TextOriginX = origin.x + metrics.Gutter.TotalWidth;
			canvas.ContentWidth = ContentWidthOf(item, metrics, available, wordWrap);
			canvas.WindowWidth = ImGui::GetWindowWidth();
			canvas.Metrics = metrics;
			canvas.Palette = ReadEditorPalette(scheme);
			canvas.SyntaxColours = ReadSyntaxPalette(scheme);

			// Under every other layer: the current-line highlight, the gutter fill, the
			// selection and the text itself all draw after this. The rectangle matches the one
			// AddInteractiveSurface hit-tests against, so the fill never falls short of what the
			// editor actually covers.
			canvas.Draw->AddRectFilled(
				canvas.Origin,
				ImVec2(canvas.Origin.x + std::max(canvas.ContentWidth, canvas.WindowWidth),
					   canvas.Origin.y + std::max(DocumentHeightOf(item, metrics),
												  ImGui::GetWindowHeight())),
				canvas.Palette.Background);

			CaretPaint caret;

			caret.Line = buffer.ToPosition(item.Caret.Offset).Line;
			// Read once here rather than per row: the loop below only compares against it, and
			// CaretToVisual walks the caret line's break list.
			caret.Visual = Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset,
											   item.TabWidth);
			caret.Focused = focused;

			LineCacheInput lineInput{buffer, item.Find.Matches};

			lineInput.Selection = Core::SelectionRange(item.Caret);
			lineInput.CurrentMatch = item.Find.Current;
			lineInput.TabWidth = item.TabWidth;

			ReserveDocumentExtent(origin, canvas.ContentWidth, DocumentHeightOf(item, metrics));

			VisibleLineCache cache;

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(item.Rows.TotalRows()), metrics.LineHeight);

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const std::size_t rowIndex = static_cast<std::size_t>(row);
					const Core::RowRef ref = item.Rows.RowAt(rowIndex);
					const float y = origin.y + static_cast<float>(row) * metrics.LineHeight;

					CacheLine(cache, lineInput, ref.Line);

					// TokensOf caches one line at a time and returns a reference into that cache,
					// so this is only safe to hold across the rows of *this* line — exactly the
					// window this loop body is. Fetching it once per row rather than hoisting it
					// above the loop is what keeps that true: a wrapped line's rows all reach the
					// same line here in a row, so every call after the first is the cache's fast
					// path.
					const std::vector<Token>& tokens =
						syntax != nullptr ? syntax->TokensOf(buffer, ref.Line) : NoTokens();

					const RowSlice slice = SliceRow(cache, item.Rows, ref, rowIndex, y);

					DrawRow(canvas, item, cache, slice, tokens, caret);
				}
			}

			clipper.End();
		}
	}

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view)
	{
		EditorFrameResult result;

		if (!item.Document)
		{
			DrawFailure(item);
			return result;
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), view.FontSize);

		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		// Read before anything in this function can move the caret — the mouse below moves it
		// as readily as the keyboard does, and the blink has to restart for both.
		const std::size_t caretAtFrameStart = item.Caret.Offset;
		const EditorMetrics metrics = MeasureEditor(buffer);

		// Asked of the document window, before the child below becomes the current one. True
		// when this document holds the keyboard, whether the focus sits on the tab itself or
		// has already been taken by the surface inside it.
		const bool documentHoldsFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (ImGui::BeginChild("##EditorSurface", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
							  EditorSurfaceFlags()))
		{
			// Selecting a tab focuses the document window, not the surface inside it, and only
			// the surface draws a caret or reads keys — so without this, switching documents
			// left the new one with no caret and a dead keyboard until it was clicked. Handing
			// the focus down cannot steal it from elsewhere: it only runs while this document
			// already holds it.
			if (documentHoldsFocus && !ImGui::IsWindowFocused())
			{
				ImGui::SetWindowFocus();
			}

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float scrollX = ImGui::GetScrollX();
			const float available = ImGui::GetContentRegionAvail().x;

			// The same compare-and-rebuild the wrap width uses. A changed tab width invalidates
			// every column in the document — the guides, the widest line and the wrap points are
			// all measured in columns — so RefreshDerived recomputes all three.
			if (item.TabWidth != view.Indent.Width && view.Indent.Width > 0)
			{
				item.TabWidth = view.Indent.Width;
				Core::RefreshDerived(item);

				// The aim was a column measured at the old width; at the new width the same
				// column lands somewhere else. Cleared rather than converted, so the next
				// vertical move resolves it from where the caret actually is now.
				item.RowAimAt = Core::NoRowAim;
			}

			// Called every frame rather than only inside the block above: the item's width and
			// the cache's must never be able to diverge, and tying this to the same edge as
			// RefreshDerived would let a null view.Syntax on the one frame the width changes
			// desync the two for good, since the block above only fires again on the next
			// actual change. SetTabWidth's own equality guard already makes the repeated call
			// free once the two agree, so the null check is the only guard this still needs.
			if (view.Syntax != nullptr)
			{
				view.Syntax->SetTabWidth(item.TabWidth);
			}

			RewrapIfGeometryChanged(item, buffer,
									ColumnsThatFit(metrics, available, view.WordWrap));

			// Pre-edit: the mouse and keyboard handling below can still grow or shrink the
			// document before the frame is over. The interactive rectangle is allowed to
			// hit-test against a size one frame stale; the reservation the scrollbars and the
			// clipper read is computed fresh, inside DrawVisibleRows.
			const PointerState pointer = AddInteractiveSurface(
				origin, ContentWidthOf(item, metrics, available, view.WordWrap),
				DocumentHeightOf(item, metrics));

			ApplyMouseToCaret(pointer, buffer, item, metrics, origin,
							  origin.x + metrics.Gutter.TotalWidth);
			RouteMouseWheel(context);

			// The keyboard is scoped to this document's own child window, not merely to the
			// parent tab: IsWindowFocused() with no flags checks the *current* window (the
			// child pushed by BeginChild above), and only the document the user actually
			// clicked into ever becomes that window. Without this, a key press reaching any
			// visible editor — including one in another split — could move a caret the user is
			// not looking at.
			//
			// Computed once and handed to the caret drawing as well, rather than tested there
			// again: the keyboard and the blinking caret must never disagree about which editor
			// is active, and asking IsWindowFocused() twice risks exactly that if ImGui's focus
			// state ever changes between the two calls in the same frame.
			const bool focused = ImGui::IsWindowFocused();

			if (focused)
			{
				RestartBlinkOnDocumentChange(item.Path);
			}

			// A context arriving without a queue is a caller that has no input to offer, not an
			// error: this document then simply draws, exactly as an unfocused one does.
			if (focused && context.Input != nullptr)
			{
				ApplyKeyboard(*context.Input, buffer, item, view.Indent, view.Syntax,
							  RowsPerPage(metrics), caretAtFrameStart, result);
			}

			// Not merely when the window has keyboard focus, which is true on every frame it is
			// focused and would fight the user's own scrolling. Comparing against
			// caretAtFrameStart, read before the mouse and keyboard above touched anything, is
			// true exactly when a click, a drag, a motion key or an edit changed where the caret
			// sits — but it says nothing about a caret moved earlier in the same frame, before
			// this function ran, which is why PendingScroll carries that case separately.
			//
			// The list is brought up to date first: the rows below draw the highlight from it,
			// and an edit this frame has already moved the offsets it holds.
			if (item.Find.Open)
			{
				(void)RefreshFindMatchesIfStale(item, buffer);
			}

			if (item.Caret.Offset != caretAtFrameStart || item.PendingScroll)
			{
				ScrollToCaret(buffer, item, metrics, view.WordWrap);
				s_CaretMovedAt = ImGui::GetTime();
				item.PendingScroll = false;
			}

			// A null Scheme is defence, not a supported mode — DocumentWindows always fills it
			// from the ActiveScheme GraphiteLayer loads at startup, the same guard the syntax
			// cache above gets.
			DrawVisibleRows(buffer, item, metrics, origin, scrollX, available, view.WordWrap,
							focused, view.Syntax, view.Scheme != nullptr ? *view.Scheme : DarkScheme());

			// After the rows, so a click on the bar is a click on the bar: the surface's own
			// hit test has already run against a rectangle the bar now sits on top of, and a
			// nested child window takes the hover from its parent.
			if (item.Find.Open)
			{
				FindBarFrame frame;

				// The top-right, less whatever the vertical scrollbar is taking, so the bar hangs
				// beside it rather than over it.
				const float usable = ImGui::GetWindowWidth() -
									 ImGui::GetCurrentWindow()->ScrollbarSizes.x - kFindBarMargin;

				frame.Corner = ImVec2(ImGui::GetWindowPos().x + usable,
									  ImGui::GetWindowPos().y + kFindBarMargin);
				frame.Width = usable - kFindBarMargin;
				frame.DocumentFocused = documentHoldsFocus;

				DrawFindBar(item, buffer, frame);
			}
		}

		ImGui::EndChild();
		ImGui::PopFont();

		return result;
	}

	void OpenFindBar(Core::OpenItem& item)
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

		s_FindFocusFor = item.Path;
	}
}
