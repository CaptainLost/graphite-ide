#include "Editor/TextEditorWidget.h"

#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace graphite
{
    namespace
    {
        /// Space either side of the line numbers.
        constexpr float kGutterPaddingX = 8.0f;

        /// The gutter never narrows below this many digits, so scrolling from line 999 to
        /// 1000 does not shift the text sideways.
        constexpr int kMinimumGutterDigits = 3;

        /// Width, in cells, reserved to the left of the line numbers for a future
        /// breakpoint marker. Nothing is drawn here yet; the column exists so the
        /// debugger's arrival does not shift every horizontal position — clicks, caret,
        /// selection, guides — that the user has already looked at.
        constexpr float kBreakpointColumnCells = 1.0f;

        /// Width, in cells, reserved to the right of the line numbers for a future fold
        /// arrow. Nothing is drawn here yet, for the same reason as the breakpoint
        /// column above.
        constexpr float kFoldColumnCells = 1.0f;

        [[nodiscard]] int DigitCount(std::size_t value)
        {
            int digits = 1;

            while (value >= 10)
            {
                value /= 10;
                ++digits;
            }

            return digits;
        }

        void DrawFailure(const core::text::OpenItem& item)
        {
            const std::string message = core::text::FailureMessage(item);
            const ImVec2 size = ImGui::CalcTextSize(message.c_str());
            const ImVec2 available = ImGui::GetContentRegionAvail();

            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * 0.5f,
                                       ImGui::GetCursorPosY() + (available.y - size.y) * 0.5f));
            ImGui::TextDisabled("%s", message.c_str());
        }
    }

    float DrawEditor(core::text::OpenItem& item, float fontSize)
    {
        float zoomDelta = 0.0f;

        if (!item.document)
        {
            DrawFailure(item);
            return zoomDelta;
        }

        ImGui::PushFont(core::ImGuiRenderer::MonoFont(), fontSize);

        const core::text::TextBuffer& buffer = item.document->Buffer();
        const float lineHeight = ImGui::GetTextLineHeight();
        // Every glyph in a monospaced face advances the same, so one measurement is the
        // width of every cell.
        const float cellWidth = ImGui::CalcTextSize("0").x;

        const int digits = std::max(kMinimumGutterDigits, DigitCount(buffer.LineCount()));
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

        // Computed once when the document loaded (OpenItem::widestColumn), not walked
        // here every frame — that would cost a std::string allocation per line, exactly
        // the per-frame cost the clipper below exists to avoid.
        const float contentWidth =
            gutterWidth + static_cast<float>(item.widestColumn + 1) * cellWidth;

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

            const float documentHeight = static_cast<float>(buffer.LineCount()) * lineHeight;

            // Reserve the full document extent so both scrollbars know how far the
            // content goes. This Dummy is the *only* thing in this function allowed to
            // grow ContentSize — see the interactive rectangle below, which deliberately
            // avoids doing the same.
            ImGui::Dummy(ImVec2(contentWidth, documentHeight));
            ImGui::SetCursorScreenPos(origin);

            // The one text origin in the file: everything below that positions against
            // the text — clicks, caret, selection, guides — measures from this rather
            // than recomputing origin.x + gutterWidth a second time.
            const float textOriginX = origin.x + gutterWidth;
            const std::size_t pageLines = static_cast<std::size_t>(
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
                const auto row =
                    static_cast<std::size_t>(std::max(0.0f, (mouse.y - origin.y) / lineHeight));
                const std::size_t line = std::min(row, buffer.LineCount() - 1);

                const float relativeX = std::max(0.0f, mouse.x - textOriginX);
                const auto column =
                    static_cast<std::size_t>((relativeX + cellWidth * 0.5f) / cellWidth);

                const std::size_t offset =
                    buffer.LineStart(line)
                    + core::text::ByteOfColumn(buffer.LineText(line), column,
                                               core::text::DefaultTabWidth);

                if (pressedThisFrame)
                {
                    const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);

                    if (clicks >= 3)
                        item.caret = core::text::SelectLineAt(buffer, line);
                    else if (clicks == 2)
                        item.caret = core::text::SelectWordAt(buffer, offset);
                    else
                        item.caret = core::text::PlaceAt(buffer, item.caret, offset,
                                                         ImGui::GetIO().KeyShift,
                                                         core::text::DefaultTabWidth);
                }
                else
                {
                    // Dragging always extends, whichever way it goes.
                    item.caret = core::text::PlaceAt(buffer, item.caret, offset, true,
                                                     core::text::DefaultTabWidth);
                }
            }

            if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl
                && ImGui::GetIO().MouseWheel != 0.0f)
            {
                zoomDelta = ImGui::GetIO().MouseWheel;
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
            const std::size_t caretBeforeKeyboard = item.caret.offset;

            if (focused)
            {
                const ImGuiIO& io = ImGui::GetIO();
                const bool shift = io.KeyShift;
                // On Windows, AltGr reports as Ctrl+Alt, not as a key of its own — so
                // typing a Polish letter that needs AltGr (ą, ć, ę, ...) would otherwise
                // read as Ctrl+<letter> here. Excluding KeyAlt keeps every shortcut in
                // this block, not just Ctrl+A and Ctrl+C, from firing on those letters.
                const bool control = io.KeyCtrl && !io.KeyAlt;

                struct Binding
                {
                    ImGuiKey key;
                    core::text::Motion plain;
                    core::text::Motion withControl;
                };

                const Binding bindings[] = {
                    {ImGuiKey_LeftArrow, core::text::Motion::Left, core::text::Motion::WordLeft},
                    {ImGuiKey_RightArrow, core::text::Motion::Right, core::text::Motion::WordRight},
                    {ImGuiKey_UpArrow, core::text::Motion::Up, core::text::Motion::Up},
                    {ImGuiKey_DownArrow, core::text::Motion::Down, core::text::Motion::Down},
                    {ImGuiKey_Home, core::text::Motion::LineStart,
                     core::text::Motion::DocumentStart},
                    {ImGuiKey_End, core::text::Motion::LineEnd, core::text::Motion::DocumentEnd},
                    {ImGuiKey_PageUp, core::text::Motion::PageUp, core::text::Motion::PageUp},
                    {ImGuiKey_PageDown, core::text::Motion::PageDown,
                     core::text::Motion::PageDown},
                };

                for (const Binding& binding : bindings)
                {
                    if (!ImGui::IsKeyPressed(binding.key, true))
                        continue;

                    item.caret =
                        core::text::Move(buffer, item.caret,
                                         control ? binding.withControl : binding.plain, shift,
                                         core::text::DefaultTabWidth, pageLines);
                }

                if (control && ImGui::IsKeyPressed(ImGuiKey_A, false))
                    item.caret = core::text::SelectAll(buffer);

                if (control && ImGui::IsKeyPressed(ImGuiKey_C, false))
                {
                    const auto [from, to] = core::text::SelectionRange(item.caret);

                    // Substring hands back the buffer's own LF-only text, and the Windows
                    // clipboard is CRLF — pasted into Excel or any Win32 EDIT control,
                    // LF-only text arrives as a single line.
                    if (from != to)
                    {
                        const std::string clipboard = core::text::WithLineEnding(
                            buffer.Substring(from, to), core::text::LineEnding::Crlf);

                        ImGui::SetClipboardText(clipboard.c_str());
                    }
                }
            }

            // Scrolling follows the caret only when a key actually moved it this frame —
            // not merely when the window has keyboard focus, which is true on every frame
            // it is focused and would fight the user's own scrolling. Comparing the
            // caret's offset before and after the keyboard block is the honest signal:
            // it is true exactly when a motion changed where the caret sits, whether or
            // not the key press also happens to change the selection.
            if (item.caret.offset != caretBeforeKeyboard)
            {
                const core::text::Position where = buffer.ToPosition(item.caret.offset);
                const float caretY = static_cast<float>(where.line) * lineHeight;

                if (caretY < ImGui::GetScrollY())
                    ImGui::SetScrollY(caretY);
                else if (caretY + lineHeight > ImGui::GetScrollY() + ImGui::GetWindowHeight())
                    ImGui::SetScrollY(caretY + lineHeight - ImGui::GetWindowHeight());
            }

            const core::text::Position caretPosition =
                buffer.ToPosition(item.caret.offset);
            const auto [selectionFrom, selectionTo] = core::text::SelectionRange(item.caret);

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

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(buffer.LineCount()), lineHeight);

            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const std::size_t line = static_cast<std::size_t>(row);
                    const float y = origin.y + static_cast<float>(row) * lineHeight;
                    const float textX = textOriginX;

                    const std::string text = buffer.LineText(line);
                    const std::size_t lineStart = buffer.LineStart(line);

                    if (line == caretPosition.line)
                    {
                        draw->AddRectFilled(
                            ImVec2(textX, y),
                            ImVec2(origin.x + std::max(contentWidth, ImGui::GetWindowWidth()),
                                   y + lineHeight),
                            currentLineColour);
                    }

                    // Under the text and the selection, so neither is ever obscured. One
                    // segment per row rather than one long line per column: a guide only
                    // exists on rows whose depth demands it, and a column's guide can stop
                    // and restart down the file.
                    const std::size_t depth =
                        line < item.indentDepths.size() ? item.indentDepths[line] : 0;

                    for (std::size_t column = 0; column < depth;
                         column += core::text::DefaultTabWidth)
                    {
                        const float guideX = textX + static_cast<float>(column) * cellWidth;

                        draw->AddLine(ImVec2(guideX, y), ImVec2(guideX, y + lineHeight),
                                      guideColour, 1.0f);
                    }

                    if (selectionFrom != selectionTo && selectionTo > lineStart
                        && selectionFrom <= lineStart + text.size())
                    {
                        const std::size_t fromByte =
                            selectionFrom > lineStart ? selectionFrom - lineStart : 0;
                        const std::size_t toByte =
                            std::min(selectionTo - lineStart, text.size() + 1);

                        const float fromX = textX
                            + static_cast<float>(core::text::ColumnOfByte(
                                  text, fromByte, core::text::DefaultTabWidth))
                                  * cellWidth;
                        // A selection running past this line's end shows the newline as
                        // one cell of highlight, which is how every editor draws it.
                        const float toX = textX
                            + static_cast<float>(core::text::ColumnOfByte(
                                  text, std::min(toByte, text.size()),
                                  core::text::DefaultTabWidth))
                                  * cellWidth
                            + (toByte > text.size() ? cellWidth : 0.0f);

                        draw->AddRectFilled(ImVec2(fromX, y), ImVec2(toX, y + lineHeight),
                                            selectionColour);
                    }

                    if (!text.empty())
                    {
                        // Tabs are advances, not glyphs; expanding them into spaces first
                        // keeps this a single AddText call instead of one run per tab.
                        const std::string expanded =
                            core::text::ExpandTabs(text, core::text::DefaultTabWidth);
                        draw->AddText(ImVec2(textX, y), textColour, expanded.c_str());
                    }

                    // Drawn last and offset by the horizontal scroll, so the gutter stays
                    // pinned to the left while the text slides under it. The fill spans
                    // the whole gutter — both reserved margins included — so nothing
                    // scrolled draws through where they sit; the number itself stays
                    // right-aligned inside its own column.
                    const std::string number = std::to_string(line + 1);
                    const float numberX =
                        numberColumnX1 - kGutterPaddingX - ImGui::CalcTextSize(number.c_str()).x;

                    draw->AddRectFilled(ImVec2(origin.x + scrollX, y),
                                        ImVec2(origin.x + scrollX + gutterWidth, y + lineHeight),
                                        ImGui::GetColorU32(ImGuiCol_WindowBg));
                    draw->AddText(ImVec2(numberX, y),
                                  line == caretPosition.line ? textColour : gutterColour,
                                  number.c_str());

                    // Only the focused editor blinks a caret: an unfocused pane still
                    // shows its selection above (a user glancing at a split needs to see
                    // what is selected there), but drawing a second blinking caret when
                    // only one editor receives the keyboard is what this guards against.
                    if (focused && line == caretPosition.line)
                    {
                        const float caretX =
                            textX
                            + static_cast<float>(core::text::ColumnOfByte(
                                  text, caretPosition.column, core::text::DefaultTabWidth))
                                  * cellWidth;

                        // A blink that resets on movement, so the caret cannot be invisible
                        // under a held arrow key. The reset lives with input handling; here
                        // it is simply time-based.
                        if (ImGui::GetTime() - static_cast<double>(static_cast<int>(
                                                    ImGui::GetTime() * 2.0) / 2)
                            < 0.5)
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

        return zoomDelta;
    }
}
