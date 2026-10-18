#include "Sessions/Requests/SentTextView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"

#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/FontSet.h"
#include "UI/ClipboardCopy.h"
#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"
#include "Sessions/SessionText.h"
#include "UI/FittedText.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kMarginFontFraction = 0.6f;
		constexpr float kSelectionBarFontFraction = 0.2f;
		constexpr float kSelectionBarMinimumWidth = 2.0f;
		constexpr float kMinimumCellWidth = 1.0f;
		constexpr float kMinimumRowButtonWidth = 1.0f;
		constexpr float kMinimumColumns = 8.0f;
		constexpr float kSelectedTintAlpha = 0.16f;
		constexpr float kHoveredTintAlpha = 0.07f;
		constexpr float kFlushRowSpacing = 0.0f;
		constexpr float kUnlimitedWidth = FLT_MAX;
		constexpr float kNoWrapWidth = 0.0f;
		constexpr float kScrollTop = 0.0f;

		ImU32 Tinted(ImU32 colour, float alpha)
		{
			return ImGui::GetColorU32(Core::WithAlphaReplaced(ImGui::ColorConvertU32ToFloat4(colour), alpha));
		}

		ItemLook LookOfSegment(const ApiRequest& request, std::size_t segment, const SessionPalette& palette)
		{
			if (segment < request.Sent.size())
			{
				return LookOf(request.Sent[segment], palette);
			}

			return LookOf(request.Response[segment - request.Sent.size()], palette);
		}

		bool IsQuiet(const ApiRequest& request, std::size_t segment)
		{
			if (segment >= request.Sent.size())
			{
				return false;
			}

			switch (request.Sent[segment].Kind)
			{
			case SentKind::ToolResult:
			case SentKind::Prompt:
			case SentKind::QueuedPrompt:
			case SentKind::Image:
				return false;
			case SentKind::Injected:
			case SentKind::ToolsLoaded:
			case SentKind::Skills:
			case SentKind::McpInstructions:
			case SentKind::EditedFile:
			case SentKind::Reminder:
			case SentKind::SystemPrompt:
			case SentKind::ToolSchemas:
				return true;
			}

			return true;
		}

		std::string TabsExpanded(std::string_view text)
		{
			std::string expanded;
			expanded.reserve(text.size());

			for (const char character : text)
			{
				if (character == '\t')
				{
					expanded.append(kTabColumns, ' ');
				}
				else
				{
					expanded.push_back(character);
				}
			}

			return expanded;
		}
	}

	std::optional<std::size_t> SentTextView::Draw(const ApiRequest& request, std::uint64_t version,
												  std::optional<std::size_t> selected, std::optional<std::size_t> hovered,
												  const SessionPalette& palette, Core::EventBus& events)
	{
		Core::PushCodeFont();

		// WARNING: Asked of ImGui rather than of the renderer, so a context without a code face draws in the face it has instead of dereferencing a null.
		ImFont* const monospaceFont = ImGui::GetFont();
		const float fontSize = ImGui::GetFontSize();
		const float cellWidth = monospaceFont->CalcTextSizeA(fontSize, kUnlimitedWidth, kNoWrapWidth, "M").x;
		const float lineHeight = ImGui::GetTextLineHeight();
		const float margin = std::round(fontSize * kMarginFontFraction);
		const float selectionBarWidth =
			std::max(std::round(fontSize * kSelectionBarFontFraction), kSelectionBarMinimumWidth);
		const float textWidth = ImGui::GetContentRegionAvail().x - margin;
		const std::size_t columns = static_cast<std::size_t>(
			std::max(textWidth / std::max(cellWidth, kMinimumCellWidth), kMinimumColumns));

		const bool isOtherRequest = !m_Built || request.Key != m_BuiltKey;

		if (isOtherRequest || version != m_BuiltVersion || columns != m_BuiltColumns)
		{
			std::vector<std::string_view> segmentTexts;

			for (std::size_t segment = 0; segment < SegmentCount(request); ++segment)
			{
				segmentTexts.push_back(DisplayText(request, segment));
			}

			m_Layout.Build(segmentTexts, columns);

			if (isOtherRequest)
			{
				ImGui::SetScrollY(kScrollTop);
				m_MenuSegment.reset();
				m_PendingScroll.reset();
			}

			m_BuiltKey = request.Key;
			m_BuiltVersion = version;
			m_BuiltColumns = columns;
			m_Built = true;
		}

		const std::vector<SentTextRow>& rows = m_Layout.GetRows();

		if (m_PendingScroll)
		{
			ImGui::SetScrollY(static_cast<float>(m_Layout.FirstRowOf(*m_PendingScroll)) * lineHeight);
			m_PendingScroll.reset();
		}

		std::optional<std::size_t> clickedSegment;
		std::optional<std::size_t> segmentUnderMouse;
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kFlushRowSpacing));

		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(rows.size()), lineHeight);

		while (clipper.Step())
		{
			for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
			{
				const SentTextRow& row = rows[static_cast<std::size_t>(rowIndex)];
				const ImVec2 rowOrigin = ImGui::GetCursorScreenPos();
				const float right = rowOrigin.x + ImGui::GetContentRegionAvail().x;
				const ItemLook look = LookOfSegment(request, row.Segment, palette);
				const bool isSelected = selected == row.Segment;
				const bool isHovered = hovered == row.Segment;

				if (isSelected || isHovered)
				{
					const float tintAlpha = isSelected ? kSelectedTintAlpha : kHoveredTintAlpha;
					drawList->AddRectFilled(rowOrigin, ImVec2(right, rowOrigin.y + lineHeight),
											Tinted(look.Colour, tintAlpha));
				}

				if (isSelected)
				{
					drawList->AddRectFilled(rowOrigin, ImVec2(rowOrigin.x + selectionBarWidth, rowOrigin.y + lineHeight),
											look.Colour);
				}

				ImVec2 textOrigin(rowOrigin.x + margin, rowOrigin.y);

				if (row.Header)
				{
					std::string column;
					std::string label;

					if (row.Segment < request.Sent.size())
					{
						column = SentColumn(request.Sent[row.Segment]);
						label = request.Sent[row.Segment].Label;
					}
					else
					{
						label = request.Response[row.Segment - request.Sent.size()].Label;
					}

					Core::PushInterfaceFont();

					const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
					DrawPiece(textOrigin, look.Colour, std::string{look.Icon} + " ");

					const float columnX = std::max(right - ImGui::CalcTextSize(column.c_str()).x, textOrigin.x);
					DrawPiece(textOrigin, look.Colour,
							  FitToWidth(SingleLineLabel(label, kLongestLabelBytes), columnX - itemSpacing - textOrigin.x));

					textOrigin.x = columnX;
					DrawPiece(textOrigin, palette.TextDisabled, FitToWidth(column, right - textOrigin.x));

					ImGui::PopFont();
				}
				else
				{
					const std::string rowText =
						TabsExpanded(DisplayText(request, row.Segment).substr(row.Begin, row.End - row.Begin));
					const bool isQuiet = IsQuiet(request, row.Segment) || IsEmptyThinking(request, row.Segment);
					drawList->AddText(monospaceFont, fontSize, textOrigin, isQuiet ? palette.TextDisabled : textColour,
									  rowText.data(), rowText.data() + rowText.size());
				}

				ImGui::PushID(rowIndex);

				if (ImGui::InvisibleButton("##row", ImVec2(std::max(right - rowOrigin.x, kMinimumRowButtonWidth), lineHeight)))
				{
					clickedSegment = row.Segment;
				}

				if (ImGui::IsItemHovered())
				{
					segmentUnderMouse = row.Segment;
				}

				ImGui::PopID();
			}
		}

		ImGui::PopStyleVar();

		ImGui::PopFont();

		const bool clickedHere = ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right);

		if (clickedHere)
		{
			m_MenuSegment = segmentUnderMouse;
		}

		if (Core::BeginRightClickMenu("##sentTextMenu", clickedHere))
		{
			const bool haveItem = m_MenuSegment.has_value() && *m_MenuSegment < SegmentCount(request);
			const bool itemIsNotChecked = false;

			if (ImGui::MenuItem("Copy this item", nullptr, itemIsNotChecked, haveItem))
			{
				CopyToClipboard(events, "item", std::string{SegmentText(request, *m_MenuSegment)});
			}

			if (ImGui::MenuItem("Copy all"))
			{
				CopyToClipboard(events, "request text", AllSegmentText(request));
			}

			ImGui::EndPopup();
		}

		return clickedSegment;
	}
}
