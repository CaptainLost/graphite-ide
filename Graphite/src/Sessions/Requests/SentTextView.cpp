#include "Sessions/Requests/SentTextView.h"

#include "Core/Base/Base.h"

#include "Core/Base/Narrow.h"

#include "Core/ImGui/ImGuiRenderer.h"
#include "Editor/StatusBar/ClipboardCopy.h"
#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"
#include "Sessions/SessionText.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {
		// A header's label is cut to one line and this much before it is fitted, as in the list.
		constexpr std::size_t kLabelBytes = 400;

		ImU32 Tinted(ImU32 colour, float alpha)
		{
			ImVec4 tint = ImGui::ColorConvertU32ToFloat4(colour);
			tint.w = alpha;
			return ImGui::GetColorU32(tint);
		}

		// Whether `segment` is the empty-thinking block DisplayText adds its note to, so the
		// note draws dimmed like other quiet text rather than at full strength.
		bool IsHiddenThinkingNote(const ApiRequest& request, std::size_t segment)
		{
			if (segment < request.Sent.size())
			{
				return false;
			}

			const std::size_t response = segment - request.Sent.size();

			return response < request.Response.size() && request.Response[response].Kind == ResponseKind::Thinking &&
				   request.Response[response].Text.empty();
		}

		ItemLook LookOfSegment(const ApiRequest& request, std::size_t segment, const SessionPalette& palette)
		{
			if (segment < request.Sent.size())
			{
				return LookOf(request.Sent[segment], palette);
			}

			return LookOf(request.Response[segment - request.Sent.size()], palette);
		}

		// What the reader wrote or the model answered reads at full strength; what Claude Code
		// added on its own is dimmed, as it is in the conversation.
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

		// A row's text with each tab laid out as the columns WrapLines counted for it.
		std::string Expanded(std::string_view text)
		{
			std::string expanded;
			expanded.reserve(text.size());

			for (const char c : text)
			{
				if (c == '\t')
				{
					expanded.append(kTabColumns, ' ');
				}
				else
				{
					expanded.push_back(c);
				}
			}

			return expanded;
		}
	}

	std::optional<std::size_t> SentTextView::Draw(const ApiRequest& request, std::uint64_t version,
												  std::optional<std::size_t> selected, std::optional<std::size_t> hovered,
												  const SessionPalette& palette, Core::EventBus& events)
	{
		ImFont* mono = Core::ImGuiRenderer::GetMonoFont();
		const float fontSize = ImGui::GetFontSize();
		const float cell = mono->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, "M").x;
		const float lineHeight = ImGui::GetTextLineHeight();
		const float margin = std::round(fontSize * 0.6f);
		const float bar = std::max(std::round(fontSize * 0.2f), 2.0f);
		const float textWidth = ImGui::GetContentRegionAvail().x - margin;
		const std::size_t columns = static_cast<std::size_t>(std::max(textWidth / std::max(cell, 1.0f), 8.0f));

		const bool otherRequest = !m_Built || request.Key != m_BuiltKey;

		if (otherRequest || version != m_BuiltVersion || columns != m_BuiltColumns)
		{
			std::vector<std::string_view> texts;

			for (std::size_t i = 0; i < SegmentCount(request); ++i)
			{
				texts.push_back(DisplayText(request, i));
			}

			m_Layout.Build(texts, columns);

			if (otherRequest)
			{
				// A scroll asked for on the request shown before names an item of that one.
				ImGui::SetScrollY(0.0f);
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

		std::optional<std::size_t> clicked;
		std::optional<std::size_t> underMouse;
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		// Rows sit flush, so the clipper's row height is the line height and nothing more.
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));

		ImGuiListClipper clipper;
		clipper.Begin(Core::Narrow<int>(rows.size()), lineHeight);

		while (clipper.Step())
		{
			for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
			{
				const SentTextRow& row = rows[static_cast<std::size_t>(r)];
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const float right = at.x + ImGui::GetContentRegionAvail().x;
				const ItemLook look = LookOfSegment(request, row.Segment, palette);
				const bool isSelected = selected == row.Segment;
				const bool isHovered = hovered == row.Segment;

				if (isSelected || isHovered)
				{
					draw->AddRectFilled(at, ImVec2(right, at.y + lineHeight), Tinted(look.Colour, isSelected ? 0.16f : 0.07f));
				}

				if (isSelected)
				{
					draw->AddRectFilled(at, ImVec2(at.x + bar, at.y + lineHeight), look.Colour);
				}

				ImVec2 textAt(at.x + margin, at.y);

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

					const float space = ImGui::GetStyle().ItemSpacing.x;
					DrawPiece(textAt, look.Colour, std::string{look.Icon} + " ");

					const float columnX = std::max(right - ImGui::CalcTextSize(column.c_str()).x, textAt.x);
					DrawPiece(textAt, look.Colour, Fitted(SingleLineLabel(label, kLabelBytes), columnX - space - textAt.x));

					textAt.x = columnX;
					DrawPiece(textAt, palette.TextDisabled, Fitted(column, right - textAt.x));
				}
				else
				{
					const std::string text =
						Expanded(DisplayText(request, row.Segment).substr(row.Begin, row.End - row.Begin));
					const bool quiet = IsQuiet(request, row.Segment) || IsHiddenThinkingNote(request, row.Segment);
					draw->AddText(mono, fontSize, textAt, quiet ? palette.TextDisabled : ink, text.data(),
								  text.data() + text.size());
				}

				// The row's item: it makes the row one line tall and takes its clicks.
				ImGui::PushID(r);

				if (ImGui::InvisibleButton("##row", ImVec2(std::max(right - at.x, 1.0f), lineHeight)))
				{
					clicked = row.Segment;
				}

				if (ImGui::IsItemHovered())
				{
					underMouse = row.Segment;
				}

				ImGui::PopID();
			}
		}

		ImGui::PopStyleVar();

		if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		{
			m_MenuSegment = underMouse;
			ImGui::OpenPopup("##sentTextMenu");
		}

		if (ImGui::BeginPopup("##sentTextMenu"))
		{
			// The request may have changed under an open menu: an item it no longer has is
			// not offered.
			const bool haveItem = m_MenuSegment.has_value() && *m_MenuSegment < SegmentCount(request);

			if (ImGui::MenuItem("Copy this item", nullptr, false, haveItem))
			{
				CopyToClipboard(events, "item", std::string{SegmentText(request, *m_MenuSegment)});
			}

			if (ImGui::MenuItem("Copy all"))
			{
				CopyToClipboard(events, "request text", AllSegmentText(request));
			}

			ImGui::EndPopup();
		}

		return clicked;
	}
}
