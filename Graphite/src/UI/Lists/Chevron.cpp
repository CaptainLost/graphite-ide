#include "UI/Lists/Chevron.h"

#include "UI/Lists/ListRow.h"

#include "Core/ImGui/ImGuiColors.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace Graphite {

	namespace {
		constexpr float kHalf = 0.5f;
		constexpr float kClearAlpha = 0.0f;
		constexpr float kFramedPaddingsBeforeLabel = 3.0f;
		constexpr const char* kUpToTheTerminator = nullptr;
		constexpr bool kHideTextAfterHashes = false;

		// WARNING: Floored, because CalcTextSize rounds up and a glyph centred onto a half pixel reads blurrier than
		// the text beside it.
		void DrawChevronAt(ImVec2 rowStart, float textTop, bool open)
		{
			const char* const glyph = ChevronGlyph(open);
			const float arrowLeft = rowStart.x + ImGui::GetStyle().FramePadding.x;
			const float glyphLeft = std::floor(arrowLeft + (ImGui::GetFontSize() - ImGui::CalcTextSize(glyph).x) * kHalf);

			ImGui::GetWindowDrawList()->AddText(ImVec2(glyphLeft, textTop), ImGui::GetColorU32(ImGuiCol_TextDisabled),
												glyph);
		}

		void DrawNoteAfter(ImVec2 labelTopLeft, const char* label, const char* note)
		{
			if (*note == '\0')
			{
				return;
			}

			const float labelWidth = ImGui::CalcTextSize(label, kUpToTheTerminator, kHideTextAfterHashes).x;
			const float gap = *label == '\0' ? 0.0f : ImGui::GetStyle().ItemSpacing.x;
			const ImVec2 noteTopLeft(labelTopLeft.x + labelWidth + gap, labelTopLeft.y);
			const ImVec2 clipMax(ImGui::GetItemRectMax().x, labelTopLeft.y + ImGui::GetFontSize());

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::RenderTextClipped(noteTopLeft, clipMax, note, kUpToTheTerminator, nullptr);
			ImGui::PopStyleColor();
		}

		// WARNING: The open state is kept where ImGui keeps a tree node's, under the same id, so SetNextItemOpen before
		// the row and TreePop after it work as they do with TreeNodeEx.
		bool DrawListTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label, const char* note)
		{
			ImGuiWindow* const window = ImGui::GetCurrentWindow();
			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			const float labelSpacing = ImGui::GetTreeNodeToLabelSpacing();
			const ImVec2 textTopLeft(rowStart.x + labelSpacing, ListRowTextTop(rowStart.y));

			const ImGuiID nodeId = window->GetID(id);
			const bool opens = (flags & ImGuiTreeNodeFlags_Leaf) == 0;
			bool open = ImGui::TreeNodeUpdateNextOpen(nodeId, flags);

			const bool selected = (flags & ImGuiTreeNodeFlags_Selected) != 0;
			const ImGuiSelectableFlags selectableFlags =
				(flags & ImGuiTreeNodeFlags_AllowOverlap) != 0 ? ImGuiSelectableFlags_AllowOverlap : ImGuiSelectableFlags_None;

			ImGui::PushID(id);
			const bool clicked = ListRowSelectable("##row", selected, selectableFlags);
			ImGui::PopID();

			const ImGuiContext& context = *ImGui::GetCurrentContext();
			const bool keyboardIsHere = opens && context.NavId == ImGui::GetItemID();
			const bool closedByKey = keyboardIsHere && context.NavMoveDir == ImGuiDir_Left && open;
			const bool openedByKey = keyboardIsHere && context.NavMoveDir == ImGuiDir_Right && !open;

			if (closedByKey || openedByKey)
			{
				ImGui::NavClearPreferredPosForAxis(ImGuiAxis_X);
				ImGui::NavMoveRequestCancel();
			}

			if ((clicked && opens) || closedByKey || openedByKey)
			{
				open = !open;
				ImGui::TreeNodeSetOpen(nodeId, open);
			}

			// WARNING: Laid out again from the row's text top, without an item, as wide as TreeNodeEx lays out the
			// arrow and the label, so what follows with SameLine starts past the label, on the label's line; the
			// Selectable stays the last item for the caller to ask.
			constexpr bool kShowTextAfterHashes = false;
			const float labelWidth = ImGui::CalcTextSize(label, kUpToTheTerminator, kShowTextAfterHashes).x;
			ImGui::SetCursorScreenPos(ImVec2(rowStart.x, textTopLeft.y));
			ImGui::ItemSize(ImVec2(labelSpacing + labelWidth, ImGui::GetFontSize()));

			if (ImGui::IsItemVisible())
			{
				if (opens)
				{
					DrawChevronAt(rowStart, textTopLeft.y, open);
				}

				if (*label != '\0')
				{
					ImGui::RenderText(textTopLeft, label, kUpToTheTerminator, kHideTextAfterHashes);
				}

				DrawNoteAfter(textTopLeft, label, note);
			}

			if (open && (flags & ImGuiTreeNodeFlags_NoTreePushOnOpen) == 0)
			{
				ImGui::TreePushOverrideID(nodeId);
			}

			return open;
		}

		// WARNING: ImGui draws its arrow in the text colour and has no flag to leave it out, so the text colour is
		// cleared for the call. The label shares that colour, so it is drawn again here, in the row's own colour and
		// where TreeNodeBehavior puts a framed node's: the offsets below are its own.
		bool DrawFramedTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label, const char* note)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float lineTextOffset = ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset;
			const bool opens = (flags & ImGuiTreeNodeFlags_Leaf) == 0;

			const ImVec2 rowStart = ImGui::GetCursorScreenPos();
			ImVec2 textTopLeft(rowStart.x + ImGui::GetFontSize() + style.FramePadding.x * kFramedPaddingsBeforeLabel,
							   rowStart.y + std::max(style.FramePadding.y, lineTextOffset));

			// WARNING: ImGui moves a framed leaf's label to the frame's start, as it has no arrow to make room for.
			if (!opens)
			{
				textTopLeft.x = rowStart.x + style.FramePadding.x;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, Core::WithAlphaReplaced(ImGui::GetStyleColorVec4(ImGuiCol_Text), kClearAlpha));
			const bool open = ImGui::TreeNodeEx(id, flags, "%s", label);
			ImGui::PopStyleColor();

			if (!ImGui::IsItemVisible())
			{
				return open;
			}

			if (opens)
			{
				DrawChevronAt(rowStart, textTopLeft.y, open);
			}

			if (*label != '\0')
			{
				ImGui::RenderTextClipped(textTopLeft, ImGui::GetItemRectMax(), label, kUpToTheTerminator, nullptr);
			}

			DrawNoteAfter(textTopLeft, label, note);

			return open;
		}
	}

	const char* ChevronGlyph(bool open)
	{
		return open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;
	}

	void DrawChevron(bool open)
	{
		ImGui::TextDisabled("%s", ChevronGlyph(open));
	}

	float ChevronWidth()
	{
		return std::max(ImGui::CalcTextSize(ICON_MDI_CHEVRON_RIGHT).x, ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x);
	}

	bool ChevronTreeNode(const char* id, ImGuiTreeNodeFlags flags, const char* label, const char* note)
	{
		// WARNING: Nothing is submitted in a window that skips its items, and what this would ask of the last item
		// would be asked of another one.
		if (ImGui::GetCurrentWindowRead()->SkipItems)
		{
			return false;
		}

		if ((flags & ImGuiTreeNodeFlags_Framed) != 0)
		{
			return DrawFramedTreeNode(id, flags, label, note);
		}

		return DrawListTreeNode(id, flags, label, note);
	}
}
