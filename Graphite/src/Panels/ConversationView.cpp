#include "Core/Base/Base.h"

#include "Panels/ConversationView.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <nlohmann/json.hpp>

#include <imgui.h>

#include <algorithm>

namespace Graphite {

	namespace {
		const ImVec4 kPromptColour{0.55f, 0.75f, 1.0f, 1.0f};
		const ImVec4 kRemovedColour{0.9f, 0.45f, 0.4f, 1.0f};
		const ImVec4 kAddedColour{0.5f, 0.8f, 0.45f, 1.0f};

		std::string Pretty(const std::string& json)
		{
			const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);
			return parsed.is_discarded() ? json
										 : parsed.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		std::string DurationText(const ToolCall& tool)
		{
			const auto start = ParseTimestamp(tool.StartedAt);
			const auto end = ParseTimestamp(tool.FinishedAt);

			if (!start || !end)
				return {};

			const auto span = *end - *start;
			if (span < std::chrono::seconds{1})
				return std::to_string(span.count()) + "ms";

			return FormatDuration(span);
		}
	}

	void ConversationView::Rebuild(const Session& session, float width)
	{
		const bool sameSession = m_BuiltGeneration != UINT64_MAX && session.GetGeneration() == m_BuiltGeneration;
		const std::size_t keep = sameSession ? m_Rows.size() : 0;

		// A session only grows within one generation, so rows already built stay valid and
		// keep their measured heights; a different generation (the transcript was read again
		// from nothing) or a new width starts over. The version is not used to detect this:
		// it resets to zero on a restart, so it is not monotonic across one and cannot tell
		// "the same session, grown" from "a different session that happens to be as long".
		std::vector<Row> rows;

		for (std::size_t turn = 0; turn < session.GetTurns().size(); ++turn)
		{
			if (!session.GetTurns()[turn].Prompt.empty())
				rows.push_back({RowKind::Prompt, turn, 0});

			for (std::size_t item = 0; item < session.GetTurns()[turn].Items.size(); ++item)
				rows.push_back({RowKind::Item, turn, item});
		}

		const bool widthChanged = width != m_BuiltWidth;

		if (!sameSession)
			m_Expanded.clear();

		m_Heights.resize(rows.size());
		m_Measured.resize(rows.size(), false);

		for (std::size_t index = 0; index < rows.size(); ++index)
		{
			if (index >= keep || widthChanged)
			{
				m_Heights[index] = Estimate(session, rows[index], width);
				m_Measured[index] = false;
			}
		}

		m_Rows = std::move(rows);
		m_BuiltVersion = session.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_BuiltWidth = width;
		m_OffsetsDirty = true;
	}

	float ConversationView::Estimate(const Session& session, const Row& row, float width) const
	{
		const float line = ImGui::GetTextLineHeightWithSpacing();
		const Turn& turn = session.GetTurns()[row.Turn];

		if (row.Kind == RowKind::Prompt)
			return ImGui::CalcTextSize(turn.Prompt.c_str(), nullptr, false, width).y + line;

		const TurnItem& item = turn.Items[row.Item];

		if (item.Kind == ItemKind::Text)
			return ImGui::CalcTextSize(item.Text.c_str(), nullptr, false, width).y + ImGui::GetStyle().ItemSpacing.y;

		// Thinking, tools and unknown blocks start as one closed line.
		return line;
	}

	void ConversationView::DrawTool(const ToolCall& tool, std::size_t row, Core::EventBus& events)
	{
		const bool expanded = m_Expanded.contains(row);
		const char* chevron = expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;
		const char* status = !tool.HasResult ? "" : tool.IsError ? ICON_MDI_CLOSE : ICON_MDI_CHECK;

		const std::string header = std::string{chevron} + " " + tool.Name + "  " +
								   TruncateUtf8(ToolSummary(tool), 200) + "  " + DurationText(tool) + " " + status;

		if (ImGui::Selectable(header.c_str(), false, ImGuiSelectableFlags_None))
		{
			if (expanded)
				m_Expanded.erase(row);
			else
				m_Expanded.insert(row);

			m_Measured[row] = false;
			m_OffsetsDirty = true;
		}

		if (!expanded)
			return;

		ImGui::Indent();

		if (const std::optional<std::filesystem::path> path = ToolFilePath(tool))
		{
			const std::u8string text = path->u8string();

			if (ImGui::SmallButton(reinterpret_cast<const char*>(text.c_str())))
				events.Publish(OpenFileRequested{*path, false});
		}

		if (const std::vector<std::string> lines = EditLines(tool); !lines.empty())
		{
			for (const std::string& line : lines)
				ImGui::TextColored(line.starts_with("- ") ? kRemovedColour : kAddedColour, "%s", line.c_str());
		}
		else
		{
			ImGui::TextWrapped("%s", Pretty(tool.InputJson).c_str());
		}

		if (tool.HasResult)
		{
			ImGui::Separator();

			if (tool.IsError)
				ImGui::TextColored(kRemovedColour, "%s", tool.Result.c_str());
			else
				ImGui::TextWrapped("%s", tool.Result.c_str());
		}

		ImGui::Unindent();
	}

	void ConversationView::DrawRow(const Session& session, std::size_t index, Core::EventBus& events)
	{
		const Row& row = m_Rows[index];
		const Turn& turn = session.GetTurns()[row.Turn];

		ImGui::PushID(static_cast<int>(index));

		if (row.Kind == RowKind::Prompt)
		{
			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_Text, kPromptColour);
			ImGui::TextWrapped("%s", turn.Prompt.c_str());
			ImGui::PopStyleColor();
		}
		else
		{
			const TurnItem& item = turn.Items[row.Item];

			switch (item.Kind)
			{
			case ItemKind::Text:
				ImGui::TextWrapped("%s", item.Text.c_str());
				break;
			case ItemKind::Thinking:
			{
				const bool expanded = m_Expanded.contains(index);
				const std::string label = std::string{expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT} +
										  (item.Text.empty() ? " thinking (not shown by the API)" : " thinking");

				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

				if (ImGui::Selectable(label.c_str()))
				{
					if (expanded)
						m_Expanded.erase(index);
					else
						m_Expanded.insert(index);

					m_Measured[index] = false;
					m_OffsetsDirty = true;
				}

				if (expanded && !item.Text.empty())
					ImGui::TextWrapped("%s", item.Text.c_str());

				ImGui::PopStyleColor();
				break;
			}
			case ItemKind::Tool:
				DrawTool(session.GetTools()[item.Tool], index, events);
				break;
			case ItemKind::Other:
				ImGui::TextDisabled("<%s>", item.Text.c_str());
				break;
			}
		}

		ImGui::PopID();
	}

	void ConversationView::Draw(const Session& session, Core::EventBus& events)
	{
		if (!ImGui::BeginChild("##conversation", ImVec2(0, 0), ImGuiChildFlags_None,
							   ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		const float width = ImGui::GetContentRegionAvail().x;

		if (session.GetVersion() != m_BuiltVersion || session.GetGeneration() != m_BuiltGeneration ||
			width != m_BuiltWidth)
			Rebuild(session, width);

		if (m_OffsetsDirty)
		{
			m_Offsets.assign(m_Rows.size() + 1, 0.0f);

			for (std::size_t index = 0; index < m_Rows.size(); ++index)
				m_Offsets[index + 1] = m_Offsets[index] + m_Heights[index];

			m_OffsetsDirty = false;
		}

		const float top = ImGui::GetScrollY();
		const float bottom = top + ImGui::GetWindowHeight();
		const float atEnd = ImGui::GetScrollMaxY();
		const bool wasAtEnd = atEnd <= 0.0f || top >= atEnd - 1.0f;

		// Rows overlapping the visible band, plus one either side so a row half in view is drawn.
		const auto first = std::upper_bound(m_Offsets.begin(), m_Offsets.end(), top);
		std::size_t begin = first == m_Offsets.begin() ? 0 : static_cast<std::size_t>(first - m_Offsets.begin()) - 1;
		begin = begin > 0 ? begin - 1 : 0;

		const float startY = ImGui::GetCursorPosY();

		for (std::size_t index = begin; index < m_Rows.size() && m_Offsets[index] < bottom + m_Heights[index]; ++index)
		{
			ImGui::SetCursorPosY(startY + m_Offsets[index]);
			const float before = ImGui::GetCursorPosY();

			DrawRow(session, index, events);

			const float taken = ImGui::GetCursorPosY() - before;

			if (!m_Measured[index] || taken != m_Heights[index])
			{
				m_Heights[index] = taken;
				m_Measured[index] = true;
				m_OffsetsDirty = true;
			}
		}

		// The child's scroll range is the whole conversation, drawn or not. The Dummy closes
		// the layout: a SetCursorPos with no item after it is an assertion failure in ImGui.
		ImGui::SetCursorPosY(startY + m_Offsets.back());
		ImGui::Dummy(ImVec2(0.0f, 0.0f));

		// A live session keeps its newest line in view while the reader is at the end, and
		// stops following the moment they scroll up.
		if (m_FollowTail && wasAtEnd)
			ImGui::SetScrollHereY(1.0f);

		m_FollowTail = wasAtEnd;

		ImGui::EndChild();
	}
}
