#include "Editor/TextEditorWidget.h"

#include "Core/Gui/ImGuiRenderer.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace graphite
{
    namespace
    {
        /// Space either side of the line numbers.
        constexpr float kGutterPaddingX = 8.0f;

        /// The gutter never narrows below this many digits, so scrolling from line 999 to
        /// 1000 does not shift the text sideways.
        constexpr int kMinimumGutterDigits = 3;

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

        /// Expands every tab in `line` into the spaces that reach its next tab stop, so
        /// the whole line can be drawn with a single AddText call. Tabs are advances
        /// rather than glyphs, so drawing the raw text directly would leave every
        /// character after the first tab in the wrong place; this scratch string sidesteps
        /// that without splitting the draw call into runs. Reuses ColumnOfByte, which
        /// already knows how a tab and a non-ASCII byte each affect the column, instead of
        /// re-deriving that here.
        [[nodiscard]] std::string ExpandTabs(std::string_view line, std::size_t tabWidth)
        {
            std::string expanded;
            expanded.reserve(line.size());

            for (std::size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '\t')
                {
                    const std::size_t column = core::text::ColumnOfByte(line, i, tabWidth);
                    const std::size_t nextColumn =
                        core::text::ColumnOfByte(line, i + 1, tabWidth);
                    expanded.append(nextColumn - column, ' ');
                }
                else
                {
                    expanded.push_back(line[i]);
                }
            }

            return expanded;
        }
    }

    void DrawEditor(core::text::OpenItem& item, float fontSize)
    {
        if (!item.document)
        {
            DrawFailure(item);
            return;
        }

        ImGui::PushFont(core::ImGuiRenderer::MonoFont(), fontSize);

        const core::text::TextBuffer& buffer = item.document->Buffer();
        const float lineHeight = ImGui::GetTextLineHeight();
        // Every glyph in a monospaced face advances the same, so one measurement is the
        // width of every cell.
        const float cellWidth = ImGui::CalcTextSize("0").x;

        const int digits = std::max(kMinimumGutterDigits, DigitCount(buffer.LineCount()));
        const float gutterWidth =
            static_cast<float>(digits) * cellWidth + kGutterPaddingX * 2.0f;

        // Computed once when the document loaded (OpenItem::widestColumn), not walked
        // here every frame — that would cost a std::string allocation per line, exactly
        // the per-frame cost the clipper below exists to avoid.
        //
        // Includes kGutterPaddingX once for the same reason textX does below: text gets
        // the same breathing room off the gutter separator that the line numbers get off
        // the window edge, so the reserved width has to grow to match.
        const float contentWidth = gutterWidth + kGutterPaddingX
            + static_cast<float>(item.widestColumn + 1) * cellWidth;

        const ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar;

        if (ImGui::BeginChild("##EditorSurface", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, flags))
        {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float scrollX = ImGui::GetScrollX();
            const ImVec2 origin = ImGui::GetCursorScreenPos();

            // Reserve the full extent so both scrollbars know how far the content goes.
            ImGui::Dummy(ImVec2(contentWidth,
                                static_cast<float>(buffer.LineCount()) * lineHeight));
            ImGui::SetCursorScreenPos(origin);

            // Matches textX below exactly: both measure from the same gutter-separator
            // padding, or a click would land a character or two off from where the glyphs
            // it is aiming at are actually drawn.
            const float textOriginX = origin.x + gutterWidth + kGutterPaddingX;
            const std::size_t pageLines = static_cast<std::size_t>(
                std::max(1.0f, ImGui::GetWindowHeight() / lineHeight));

            // A click anywhere in the surface takes keyboard focus, which is what makes
            // the arrow keys reach this window rather than the dockspace.
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                ImGui::SetKeyboardFocusHere();

            if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
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

                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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

            // The keyboard is scoped to this document's own child window, not merely to
            // the parent tab: IsWindowFocused() with no flags checks the *current* window
            // (the child pushed by BeginChild above), and only the document the user
            // actually clicked into ever becomes that window. Without this, a key press
            // reaching any visible editor — including one in another split — could move a
            // caret the user is not looking at.
            const std::size_t caretBeforeKeyboard = item.caret.offset;

            if (ImGui::IsWindowFocused())
            {
                const ImGuiIO& io = ImGui::GetIO();
                const bool shift = io.KeyShift;
                const bool control = io.KeyCtrl;

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

                    if (from != to)
                        ImGui::SetClipboardText(buffer.Substring(from, to).c_str());
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

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(buffer.LineCount()), lineHeight);

            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    const std::size_t line = static_cast<std::size_t>(row);
                    const float y = origin.y + static_cast<float>(row) * lineHeight;
                    // Off the gutter separator by kGutterPaddingX, the same air the line
                    // numbers get off the window's edge - without it the separator sits
                    // one pixel from the first glyph and reads as part of the text.
                    const float textX = origin.x + gutterWidth + kGutterPaddingX;

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
                            ExpandTabs(text, core::text::DefaultTabWidth);
                        draw->AddText(ImVec2(textX, y), textColour, expanded.c_str());
                    }

                    // Drawn last and offset by the horizontal scroll, so the numbers stay
                    // pinned to the left while the text slides under them.
                    const std::string number = std::to_string(line + 1);
                    const float numberX = origin.x + scrollX + gutterWidth - kGutterPaddingX
                                          - ImGui::CalcTextSize(number.c_str()).x;

                    draw->AddRectFilled(ImVec2(origin.x + scrollX, y),
                                        ImVec2(origin.x + scrollX + gutterWidth, y + lineHeight),
                                        ImGui::GetColorU32(ImGuiCol_WindowBg));
                    draw->AddText(ImVec2(numberX, y),
                                  line == caretPosition.line ? textColour : gutterColour,
                                  number.c_str());

                    if (line == caretPosition.line)
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

            // Drawn once, after every row's gutter background, rather than once per row:
            // a segment per line left visible seams where anti-aliased edges met, which
            // read as a stack of short bars instead of a single rule - and drawing it
            // before the loop instead of after would have it painted back over by each
            // row's gutter background rectangle, reintroducing the same seams by a
            // different route. Anchored to the child window's own screen rect rather than
            // to document coordinates (unlike a row's y, which is document-relative and
            // scrolls off-screen), since the divider between gutter and text should always
            // cover the current view regardless of how far the document has scrolled
            // vertically. Pinned horizontally the same way the gutter background and
            // numbers are, by cancelling the scroll baked into origin.x.
            const float separatorX = origin.x + scrollX + gutterWidth;
            const float viewTop = ImGui::GetWindowPos().y;
            draw->AddLine(ImVec2(separatorX, viewTop),
                          ImVec2(separatorX, viewTop + ImGui::GetWindowHeight()), gutterColour);
        }

        ImGui::EndChild();
        ImGui::PopFont();
    }
}
