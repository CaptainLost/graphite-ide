#include "UI/Lists/ListRow.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace Graphite {

	namespace {
		constexpr float kListRowSpacing = 9.0f;
		constexpr float kHighlightInset = 6.0f;
		constexpr float kHighlightRounding = 5.0f;
		constexpr float kEndInset = 8.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kSelectedAlpha = 0.20f;
		constexpr float kPressedAlpha = 0.30f;
		constexpr float kHoverLiftTowardTheText = 0.06f;
		constexpr ImVec4 kClear(0.0f, 0.0f, 0.0f, 0.0f);

		constexpr std::array kColoursSelectableDraws{ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
													 ImGuiCol_NavCursor};

		// WARNING: Selectable fills its whole box square, and the box is as wide as the list so that a click anywhere
		// across it lands on the row. Its fill and its keyboard mark are cleared for the call and drawn again inset;
		// cleared, they add nothing to the draw list.
		void PushClearedSelectableColours()
		{
			for (const ImGuiCol colour : kColoursSelectableDraws)
			{
				ImGui::PushStyleColor(colour, kClear);
			}
		}

		void PopClearedSelectableColours()
		{
			ImGui::PopStyleColor(static_cast<int>(kColoursSelectableDraws.size()));
		}

		std::optional<ImVec4> HighlightOf(bool selected, ImGuiSelectableFlags flags)
		{
			const bool highlighted = ImGui::IsItemHovered() || (flags & ImGuiSelectableFlags_Highlight) != 0;

			return ListRowHighlight(selected, highlighted, ImGui::IsItemActive());
		}

		ImRect InsetBetween(float left, float right, float top, float bottom)
		{
			const float inset = Core::Scaled(kHighlightInset);

			return ImRect(left + inset, top, right - inset, bottom);
		}

		ImRect TableRowHighlightBetween(const ImGuiTable& table, float top, float bottom)
		{
			return InsetBetween(table.WorkRect.Min.x, table.WorkRect.Max.x, top, bottom);
		}

		void FillHighlight(const ImRect& highlight, const ImVec4& colour, bool disabled)
		{
			ImGui::BeginDisabled(disabled);
			ImGui::GetWindowDrawList()->AddRectFilled(highlight.Min, highlight.Max, ImGui::GetColorU32(colour),
													  Core::Scaled(kHighlightRounding));
			ImGui::EndDisabled();
		}

		// WARNING: Call this while the row's Selectable is the last item. A Selectable disabled by its flag draws its
		// fill disabled, so the inset fill is drawn disabled too.
		void DrawHighlight(const ImRect& highlight, bool selected, ImGuiSelectableFlags flags)
		{
			const std::optional<ImVec4> colour = HighlightOf(selected, flags);

			if (colour.has_value())
			{
				FillHighlight(highlight, *colour, (flags & ImGuiSelectableFlags_Disabled) != 0);
			}

			ImGui::RenderNavCursor(highlight, ImGui::GetItemID(), ImGuiNavRenderCursorFlags_Compact,
								   Core::Scaled(kHighlightRounding));
		}

		struct TableCellStart
		{
			ImVec2 Position;
			float TextBaseOffset = 0.0f;
			float ReachedY = 0.0f;
		};

		// WARNING: The row's Selectable is laid out from the row's top and a pitch high, so that its box is the row's
		// whole slot. Laid out so, it would end its cell, and with it the row, a cell padding below the pitch. The
		// cell is put back as one holding a single text line: what follows with SameLine starts at the cell's text,
		// anything laid out after it starts where the cell's content does, and the row ends at the pitch.
		void HoldTheTableRowLine(ImGuiWindow& window, const ImGuiTable& table, const TableCellStart& cell)
		{
			const float textLineBottom = table.RowPosY1 + ListRowPitch() - table.RowCellPaddingY;

			window.DC.CursorPos = cell.Position;
			window.DC.CursorPosPrevLine = cell.Position;
			window.DC.PrevLineSize.y = textLineBottom - cell.Position.y;
			window.DC.PrevLineTextBaseOffset = cell.TextBaseOffset;
			window.DC.CurrLineTextBaseOffset = cell.TextBaseOffset;
			window.DC.CursorMaxPos.y = std::max(cell.ReachedY, textLineBottom);
		}

		// WARNING: Callers lay a row's pieces after the Selectable, with SameLine, which goes back to the row's top,
		// or at ListRowTextTop. ImGui ends each line at its tallest piece plus the item spacing, so a line of text
		// alone would end the row short of the pitch. The row's line is therefore held for both ways in: from the
		// row's top, with the text offset as its baseline, for SameLine, and from the text top for a piece placed
		// there. A row nothing is laid over leaves the second standing for the next item: the next row clears it
		// before its own Selectable, and any other item takes a line at least that tall.
		void HoldTheRowLine(ImGuiWindow& window, float rowTop)
		{
			window.DC.PrevLineSize.y = ListRowLineHeight();
			window.DC.PrevLineTextBaseOffset = ListRowTextTop(rowTop) - rowTop;
			window.DC.CurrLineSize.y = rowTop + ListRowLineHeight() - ListRowTextTop(rowTop);
		}
	}

	ListRowColours CurrentListRowColours()
	{
		const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
		const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		return ListRowColours{.Selected = Core::WithAlphaReplaced(accent, kSelectedAlpha),
							  .Hovered = ImLerp(background, text, kHoverLiftTowardTheText),
							  .Pressed = Core::WithAlphaReplaced(accent, kPressedAlpha)};
	}

	float ListRowSpacing()
	{
		return Core::Scaled(kListRowSpacing);
	}

	float ListRowPitch()
	{
		return ImGui::GetTextLineHeight() + ListRowSpacing();
	}

	float ListRowLineHeight()
	{
		return ListRowPitch() - ImGui::GetStyle().ItemSpacing.y;
	}

	// WARNING: Centred, but never lower than leaves a text line laid there ending inside the row's line: ImGui ends
	// a line at its tallest piece plus the item spacing, and a text line reaching past the row's line would end the
	// row below the pitch. The two agree at the sizes the app is drawn at; they part only at large scales.
	float ListRowTextTop(float rowTop)
	{
		const float textLine = ImGui::GetTextLineHeight();
		const float centred = (ListRowPitch() - textLine) * kHalf;
		const float lowestInsideTheRowLine = std::max(0.0f, ListRowLineHeight() - textLine);

		return rowTop + std::floor(std::min(centred, lowestInsideTheRowLine));
	}

	float ListRowEndInset()
	{
		return Core::Scaled(kEndInset);
	}

	// WARNING: InnerRect is the window's visible inside, clear of its scrollbars. The work rect and the content
	// region move with a sideways scroll, and a row's end placed on them would slide out of view.
	float ListRowEnd()
	{
		return ListRight() - ListRowEndInset();
	}

	float ListRowEndPosX()
	{
		return ListRowEnd() - ImGui::GetWindowPos().x + ImGui::GetScrollX();
	}

	float ListLeft()
	{
		return ImGui::GetCurrentWindowRead()->InnerRect.Min.x;
	}

	float ListRight()
	{
		return ImGui::GetCurrentWindowRead()->InnerRect.Max.x;
	}

	// WARNING: No width is given. ImGui then fills the row from the cursor to the work rect's edge, but lays out
	// only the id's empty label; a width laid out would widen the list's content, and a list that scrolls sideways,
	// whose work rect is as wide as its content, would only ever grow wider. The Selectable is a pitch high with no
	// item spacing, so its box is the row's whole slot: the first row's highlight starts at the list's top, and the
	// list's content reaches the last row's bottom. AllowOverlap is given to the item as well as to the Selectable,
	// so the hover asked of the row lets go while an item laid over it is under the pointer, as Selectable's own does.
	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags)
	{
		ImGuiWindow* const window = ImGui::GetCurrentWindow();

		if (window->SkipItems)
		{
			return false;
		}

		constexpr float kToTheWorkRectEdge = 0.0f;
		constexpr float kNoItemSpacing = 0.0f;
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;

		ImGui::SetCursorScreenPos(ImVec2(ListLeft(), rowStart.y));
		window->DC.CurrLineSize.y = 0.0f;
		window->DC.CurrLineTextBaseOffset = 0.0f;

		if ((flags & ImGuiSelectableFlags_AllowOverlap) != 0)
		{
			ImGui::SetNextItemAllowOverlap();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacingX, kNoItemSpacing));
		PushClearedSelectableColours();
		const bool pressed = ImGui::Selectable(id, selected, flags, ImVec2(kToTheWorkRectEdge, ListRowPitch()));
		PopClearedSelectableColours();
		ImGui::PopStyleVar();

		HoldTheRowLine(*window, rowStart.y);

		if (ImGui::IsItemVisible())
		{
			DrawHighlight(InsetBetween(ListLeft(), ListRight(), ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y),
						  selected, flags);
		}

		return pressed;
	}

	ImVec2 ListTableCellPadding()
	{
		return ImVec2(ImGui::GetStyle().CellPadding.x, ListRowSpacing() * kHalf);
	}

	// WARNING: Not Selectable's own order, which lets the hover cover a selected row: the hover is a quieter lift
	// than the accent, and a selected row under the pointer would read as deselected. A row held down under the
	// pointer shows the press, then a selected row the accent, then a row under the pointer the hover.
	std::optional<ImVec4> ListRowHighlight(bool selected, bool hovered, bool held)
	{
		const ListRowColours colours = CurrentListRowColours();

		if (hovered && held)
		{
			return colours.Pressed;
		}

		if (selected)
		{
			return colours.Selected;
		}

		if (hovered)
		{
			return colours.Hovered;
		}

		return std::nullopt;
	}

	// WARNING: ImGui pads a table's two ends by the cell padding at most, less than the highlight's inset, so the
	// first and last cells' content would stand out of the row's highlight. The padding is set as BeginTable sets
	// it, for a padding of the row end's inset, and before the table's first row lays its columns out with it.
	void PadListTableEnds()
	{
		ImGuiTable* const table = ImGui::GetCurrentTable();

		if (table != nullptr)
		{
			table->OuterPaddingX = ListRowEndInset() - table->CellPaddingX;
		}
	}

	// WARNING: The highlight is drawn in the table's background channel, under every column and clipped only to the
	// table, so it is one shape across the row, rounded only at the row's two ends. The click target stays the
	// table's full width, and the table's rows are one text line each, as a list's are.
	bool TableRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags)
	{
		ImGuiWindow* const window = ImGui::GetCurrentWindow();
		const ImGuiTable* const table = ImGui::GetCurrentTable();

		if (window->SkipItems || table == nullptr)
		{
			return false;
		}

		constexpr float kAcrossTheTable = 0.0f;
		constexpr float kNoItemSpacing = 0.0f;
		const TableCellStart cell{.Position = window->DC.CursorPos,
								  .TextBaseOffset = window->DC.CurrLineTextBaseOffset,
								  .ReachedY = window->DC.CursorMaxPos.y};

		window->DC.CursorPos.y = table->RowPosY1;
		window->DC.CurrLineTextBaseOffset = 0.0f;
		window->DC.CurrLineSize.y = 0.0f;

		if ((flags & ImGuiSelectableFlags_AllowOverlap) != 0)
		{
			ImGui::SetNextItemAllowOverlap();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kNoItemSpacing));
		PushClearedSelectableColours();
		const bool pressed = ImGui::Selectable(id, selected, flags | ImGuiSelectableFlags_SpanAllColumns,
											   ImVec2(kAcrossTheTable, ListRowPitch()));
		PopClearedSelectableColours();
		ImGui::PopStyleVar();

		HoldTheTableRowLine(*window, *table, cell);

		if (ImGui::IsItemVisible())
		{
			ImGui::TablePushBackgroundChannel();
			DrawHighlight(TableRowHighlightBetween(*table, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().y),
						  selected, flags);
			ImGui::TablePopBackgroundChannel();
		}

		return pressed;
	}

	// WARNING: For a row with no Selectable of its own, called once its cells are drawn and before the next row
	// begins. ImGui moves a row's bottom down as it ends each cell; the cell still open has reached only as far as
	// its content, so the bottom is taken from both.
	void DrawTableRowHighlight(bool selected, bool hovered)
	{
		const ImGuiWindow* const window = ImGui::GetCurrentWindowRead();
		const ImGuiTable* const table = ImGui::GetCurrentTable();

		if (table == nullptr || table->CurrentColumn < 0)
		{
			return;
		}

		constexpr bool kNotHeld = false;
		constexpr bool kNotDisabled = false;
		const std::optional<ImVec4> colour = ListRowHighlight(selected, hovered, kNotHeld);

		if (!colour.has_value())
		{
			return;
		}

		const float rowBottom = std::max(table->RowPosY2, window->DC.CursorMaxPos.y + table->RowCellPaddingY);

		ImGui::TablePushBackgroundChannel();
		FillHighlight(TableRowHighlightBetween(*table, table->RowPosY1, rowBottom), *colour, kNotDisabled);
		ImGui::TablePopBackgroundChannel();
	}
}
