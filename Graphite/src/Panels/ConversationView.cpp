#include "Core/Base/Base.h"

#include "Panels/ConversationView.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Workspace/Workspace.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include <IconsMaterialDesignIcons.h>

#include <nlohmann/json.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <exception>
#include <format>
#include <map>
#include <string_view>
#include <tuple>

namespace Graphite {

	namespace {
		// How far a block's text sits inside its background, and a coloured bar's row text
		// from the bar.
		constexpr float kBlockPadding = 8.0f;
		constexpr float kBarGap = 8.0f;

		const char* const kEllipsis = "\xe2\x80\xa6";
		const char* const kDot = " \xc2\xb7 ";
		const char* const kTimes = " \xc3\x97";

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

		void ColouredText(ImU32 colour, const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
			ImGui::PopStyleColor();
		}

		// A thin vertical bar at the left of the row just begun, in the category's colour.
		void CategoryBar(ImU32 colour, float height)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + 3.0f, at.y + height), colour);
		}

		// Text that is cut with an ellipsis to fit `width`, never wrapped. Measured once: the
		// font says where the text stops fitting, which is always between two characters, so
		// a multi-byte character is never split.
		std::string Fitted(const std::string& text, float width)
		{
			ImFont* font = ImGui::GetFont();
			const float size = ImGui::GetFontSize();
			const char* begin = text.c_str();
			const char* end = begin + text.size();

			if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x <= width)
				return text;

			const float ellipsis = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kEllipsis).x;
			if (width < ellipsis)
				return {};

			const char* remaining = begin;
			font->CalcTextSizeA(size, width - ellipsis, 0.0f, begin, end, &remaining);

			// Backed off to a lead byte all the same, should the font ever stop mid-character.
			while (remaining > begin && (static_cast<unsigned char>(*remaining) & 0xC0u) == 0x80u)
				--remaining;

			return std::string{begin, remaining} + kEllipsis;
		}

		const char* IconOf(ToolCategory category)
		{
			switch (category)
			{
			case ToolCategory::Read: return ICON_MDI_FILE_DOCUMENT_OUTLINE;
			case ToolCategory::Edit: return ICON_MDI_PENCIL;
			case ToolCategory::Run: return ICON_MDI_CONSOLE;
			case ToolCategory::Agent: return ICON_MDI_ROBOT_OUTLINE;
			case ToolCategory::Web: return ICON_MDI_WEB;
			case ToolCategory::Other: return ICON_MDI_TOOLS;
			}
			return ICON_MDI_TOOLS;
		}

		// Draws `text` at `at` and moves `at` past it. The one-line rows are drawn through the
		// draw list over a Selectable, so each piece can take its own colour and the row stays
		// exactly one item tall.
		void DrawPiece(ImVec2& at, ImU32 colour, const std::string& text)
		{
			ImGui::GetWindowDrawList()->AddText(at, colour, text.c_str(), text.c_str() + text.size());
			at.x += ImGui::CalcTextSize(text.c_str(), text.c_str() + text.size()).x;
		}

		// A block with a background: the content is drawn on the upper channel of the window's
		// draw list, and the rectangle behind it on the lower one once its height is known.
		struct Block
		{
			ImVec2 Start;
			float Width = 0.0f;
		};

		Block BeginBlock()
		{
			ImGui::Spacing();

			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->ChannelsSplit(2);
			draw->ChannelsSetCurrent(1);

			const Block block{ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x};

			// The top padding, as tall as the item spacing the last line leaves below it.
			ImGui::Dummy(ImVec2(0.0f, 0.0f));
			ImGui::Indent(kBlockPadding);
			return block;
		}

		void EndBlock(const Block& block, ImU32 colour)
		{
			ImGui::Unindent(kBlockPadding);

			const float bottom = ImGui::GetCursorScreenPos().y;
			ImDrawList* draw = ImGui::GetWindowDrawList();

			draw->ChannelsSetCurrent(0);
			draw->AddRectFilled(block.Start, ImVec2(block.Start.x + block.Width, bottom), colour,
								ImGui::GetStyle().FrameRounding);
			draw->ChannelsMerge();
		}

		using RowIdentity = std::tuple<int, std::size_t, std::size_t, std::size_t>;
	}

	std::chrono::minutes ConversationView::OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant)
	{
		// The zone is looked up once. The lookup throws when the time zone database is not
		// available, which happens on a Windows install without the ICU data chrono reads:
		// the times then fall back to UTC rather than the view failing to draw.
		if (!m_ZoneResolved)
		{
			m_ZoneResolved = true;

			try
			{
				m_Zone = std::chrono::current_zone();
			}
			catch (const std::exception&)
			{
				m_Zone = nullptr;
			}
		}

		if (m_Zone == nullptr)
			return std::chrono::minutes{0};

		// The offset at the instant itself, so a time on the far side of a daylight saving
		// change is on the clock it was then. The zone's answer holds for a whole span, which
		// nearly every time in a session falls within, so it is asked again only outside it.
		if (!m_ZoneInfo || instant < m_ZoneInfo->begin || instant >= m_ZoneInfo->end)
		{
			try
			{
				m_ZoneInfo = m_Zone->get_info(std::chrono::floor<std::chrono::seconds>(instant));
			}
			catch (const std::exception&)
			{
				m_ZoneInfo.reset();
				return std::chrono::minutes{0};
			}
		}

		return std::chrono::floor<std::chrono::minutes>(m_ZoneInfo->offset);
	}

	std::string ConversationView::ClockTime(const std::string& timestamp)
	{
		const auto parsed = ParseTimestamp(timestamp);

		if (!parsed)
			return {};

		const auto local = std::chrono::floor<std::chrono::minutes>(*parsed) + OffsetAt(*parsed);
		const std::chrono::hh_mm_ss clock{local - std::chrono::floor<std::chrono::days>(local)};

		return std::format("{:02}:{:02}", clock.hours().count(), clock.minutes().count());
	}

	void ConversationView::Toggle(std::set<std::pair<std::size_t, std::size_t>>& open,
								  std::pair<std::size_t, std::size_t> key, std::size_t row)
	{
		if (open.contains(key))
			open.erase(key);
		else
			open.insert(key);

		m_Measured[row] = false;
		m_OffsetsDirty = true;
	}

	void ConversationView::Rebuild(const Session& session, float width)
	{
		// A session only grows within one generation, so rows already built stay valid and
		// keep their measured heights; a different generation (the transcript was read again
		// from nothing) or a new width starts over. The version is not used to detect this:
		// it resets to zero on a restart, so it is not monotonic across one and cannot tell
		// "the same session, grown" from "a different session that happens to be as long".
		const bool sameSession = m_BuiltGeneration != UINT64_MAX && session.GetGeneration() == m_BuiltGeneration;

		// Cleared before the rows are built, since the rows depend on which runs are open.
		if (!sameSession)
		{
			m_OpenParts.clear();
			m_OpenItems.clear();
		}

		const auto identity = [](const Row& row)
		{ return RowIdentity{static_cast<int>(row.Kind), row.Turn, row.Part, row.Item}; };

		// Heights are kept by what a row shows, not by its index: opening a run of steps
		// inserts rows in the middle, and every row after it would otherwise take the height of
		// whatever used to sit at its index.
		std::map<RowIdentity, std::pair<float, bool>> kept;

		if (sameSession && width == m_BuiltWidth)
		{
			for (std::size_t index = 0; index < m_Rows.size(); ++index)
				kept.emplace(identity(m_Rows[index]), std::make_pair(m_Heights[index], m_Measured[index]));
		}

		const std::vector<Turn>& turns = session.GetTurns();
		m_Parts.assign(turns.size(), {});

		// A new version may have brought a tool its result, which changes its facts.
		m_Facts.assign(session.GetTools().size(), std::nullopt);

		std::vector<Row> rows;

		// The latest time seen so far, which a gap is measured from.
		std::string last;

		const auto gapTo = [&rows, &last](std::string_view next, std::size_t turn, std::size_t part)
		{
			if (last.empty() || next.empty())
				return;

			if (const std::optional<std::chrono::milliseconds> gap = GapBetween(last, next))
				rows.push_back({RowKind::Gap, turn, part, 0, *gap});
		};

		for (std::size_t turn = 0; turn < turns.size(); ++turn)
		{
			const Turn& current = turns[turn];

			if (!current.Prompt.empty())
			{
				gapTo(current.StartedAt, turn, kBeforeUser);
				rows.push_back({RowKind::User, turn, 0, 0, {}});
			}

			if (!current.StartedAt.empty())
				last = current.StartedAt;

			m_Parts[turn] = SplitTurn(current, session.GetTools());

			for (std::size_t part = 0; part < m_Parts[turn].size(); ++part)
			{
				const TurnPart& split = m_Parts[turn][part];

				if (split.Items.empty())
					continue;

				gapTo(current.Items[split.Items.front()].At, turn, part);
				rows.push_back({RowKind::Part, turn, part, 0, {}});

				if (split.Kind == PartKind::Steps && m_OpenParts.contains({turn, part}))
				{
					for (const std::size_t item : split.Items)
						rows.push_back({RowKind::Step, turn, part, item, {}});
				}

				// When the part last did anything, not when its last item was written: a tool
				// that ran for a quarter of an hour was not a pause.
				if (std::string at = LastActivity(current, split, session.GetTools()); !at.empty())
					last = std::move(at);
			}
		}

		m_Heights.assign(rows.size(), 0.0f);
		m_Measured.assign(rows.size(), false);

		for (std::size_t index = 0; index < rows.size(); ++index)
		{
			if (const auto found = kept.find(identity(rows[index])); found != kept.end())
			{
				m_Heights[index] = found->second.first;
				m_Measured[index] = found->second.second;
			}
			else
			{
				m_Heights[index] = Estimate(session, rows[index], width);
			}
		}

		// Rows may now stand for other parts; each reply is parsed again when next drawn.
		m_Markdown.clear();

		m_Rows = std::move(rows);
		m_BuiltVersion = session.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_BuiltWidth = width;
		m_OffsetsDirty = true;
	}

	float ConversationView::Estimate(const Session& session, const Row& row, float width) const
	{
		const float line = ImGui::GetTextLineHeightWithSpacing();
		const float spacing = ImGui::GetStyle().ItemSpacing.y;
		const Turn& turn = session.GetTurns()[row.Turn];

		switch (row.Kind)
		{
		case RowKind::User:
			return ImGui::CalcTextSize(turn.Prompt.c_str(), nullptr, false, width).y + 2.0f * line;
		case RowKind::Gap:
			return line + 2.0f * spacing;
		case RowKind::Step:
			return line;
		case RowKind::Part:
			break;
		}

		const TurnPart& part = m_Parts[row.Turn][row.Part];

		switch (part.Kind)
		{
		case PartKind::Reply:
		{
			const std::string& text = turn.Items[part.Items.front()].Text;
			return ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y * 1.1f + 2.0f * line;
		}
		case PartKind::Steps:
			return part.Requests > 0 ? 2.0f * line : line;
		case PartKind::Injected:
			return line;
		case PartKind::Queued:
		{
			const std::string& text = turn.Items[part.Items.front()].Text;
			return ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y + 2.0f * line;
		}
		}

		return line;
	}

	void ConversationView::DrawUser(const std::string& text, const std::string& at, const ConversationContext& context)
	{
		const Block block = BeginBlock();

		ColouredText(context.Palette.Accent, "User");

		if (const std::string time = ClockTime(at); !time.empty())
		{
			ImGui::SameLine();
			ColouredText(context.Palette.TextDisabled, time);
		}

		// The prompt as the user typed it: not markdown, which a prompt rarely is on purpose.
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - kBlockPadding);
		ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
		ImGui::PopTextWrapPos();

		EndBlock(block, context.Palette.UserBlock);
	}

	void ConversationView::DrawReply(const Turn& turn, const TurnPart& part, std::pair<std::size_t, std::size_t> key,
									 const ConversationContext& context)
	{
		const TurnItem& item = turn.Items[part.Items.front()];
		const Block block = BeginBlock();

		ColouredText(context.Palette.Accent, "Claude");

		if (const std::string time = ClockTime(item.At); !time.empty())
		{
			ImGui::SameLine();
			ColouredText(context.Palette.TextDisabled, time);
		}

		if (part.Requests > 0)
		{
			ImGui::SameLine();
			ColouredText(context.Palette.TextDisabled,
						 Fitted(UsageLine(part.Usage, part.Requests, context.Prices),
								ImGui::GetContentRegionAvail().x - kBlockPadding));
		}

		DrawMarkdown(item.Text, key, context);

		EndBlock(block, context.Palette.ClaudeBlock);
	}

	void ConversationView::DrawStepsHeader(const Session& session, const Turn& turn, const TurnPart& part,
										   std::size_t row, const ConversationContext& context)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[row].Turn, m_Rows[row].Part};
		const bool open = m_OpenParts.contains(key);
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		// Coloured by the kind of the first call: a run is usually one kind of work, and a
		// run with a failure in it is marked as failed whatever it was.
		ImU32 bar = context.Palette.Other;
		bool finished = true;
		bool anyTool = false;

		for (const std::size_t index : part.Items)
		{
			const TurnItem& item = turn.Items[index];

			if (item.Kind != ItemKind::Tool)
				continue;

			const ToolCall& tool = session.GetTools()[item.Tool];

			if (!anyTool)
				bar = context.Palette.CategoryColor(CategoryOf(tool.Name));

			anyTool = true;
			finished = finished && tool.HasResult;
		}

		if (part.Failures > 0)
			bar = context.Palette.Failed;

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		CategoryBar(bar, lineHeight);

		if (ImGui::Selectable("##steps", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
		{
			Toggle(m_OpenParts, key, row);

			// Opening or closing a run adds or removes rows, which only a rebuild does.
			m_BuiltVersion = UINT64_MAX;
		}

		const std::size_t count = part.Items.size();
		std::string head = std::string{open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT} + " " +
						   std::to_string(count) + (count == 1 ? " step" : " steps");

		constexpr std::size_t kNamed = 3;

		for (std::size_t index = 0; index < part.ToolCounts.size() && index < kNamed; ++index)
		{
			head += index == 0 ? kDot : ", ";
			head += part.ToolCounts[index].first + kTimes + std::to_string(part.ToolCounts[index].second);
		}

		if (part.ToolCounts.size() > kNamed)
			head += ", + " + std::to_string(part.ToolCounts.size() - kNamed) + " more";

		// The duration and the status are drawn after the names, and the names are cut so
		// that they always fit.
		const std::string duration = part.Duration.count() > 0 ? kDot + FormatDuration(part.Duration) : std::string{};
		const bool showStatus = anyTool && finished;
		const std::string status = !showStatus			? std::string{}
								   : part.Failures > 0 ? std::string{ICON_MDI_CLOSE} + " " + std::to_string(part.Failures)
													   : std::string{ICON_MDI_CHECK};
		const std::string statusDot = showStatus ? kDot : "";

		const float tail = ImGui::CalcTextSize((duration + statusDot + status).c_str()).x;
		ImVec2 at(start.x + kBarGap, start.y);

		DrawPiece(at, ink, Fitted(head, right - at.x - tail));
		DrawPiece(at, context.Palette.TextDisabled, duration + statusDot);
		DrawPiece(at, part.Failures > 0 ? context.Palette.Failed : ink, status);

		if (part.Requests > 0)
		{
			ImGui::Indent(kBarGap);
			ColouredText(context.Palette.TextDisabled, Fitted(UsageLine(part.Usage, part.Requests, context.Prices),
															  ImGui::GetContentRegionAvail().x));
			ImGui::Unindent(kBarGap);
		}
	}

	void ConversationView::DrawStep(const Session& session, const TurnItem& item, std::size_t row,
									const ConversationContext& context, Core::EventBus& events)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[row].Turn, m_Rows[row].Item};
		const bool open = m_OpenItems.contains(key);
		const SessionPalette& palette = context.Palette;

		switch (item.Kind)
		{
		case ItemKind::Tool:
			break;
		case ItemKind::Thinking:
		{
			// Empty thinking is never among a run's items; this only guards the row.
			if (item.Text.empty())
				return;

			const std::string label =
				std::string{open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT} + " thinking##thinking";

			ImGui::PushStyleColor(ImGuiCol_Text, palette.TextDisabled);

			if (ImGui::Selectable(label.c_str()))
				Toggle(m_OpenItems, key, row);

			if (open)
				ImGui::TextWrapped("%s", item.Text.c_str());

			ImGui::PopStyleColor();
			return;
		}
		case ItemKind::Other:
			ColouredText(palette.TextDisabled, "<" + item.Text + ">");
			return;
		case ItemKind::Text:
		case ItemKind::Injected:
		case ItemKind::Queued:
			ImGui::TextWrapped("%s", item.Text.c_str());
			return;
		}

		const ToolCall& tool = session.GetTools()[item.Tool];

		// Facts parse the tool's result, which is too much to do for every row every frame.
		if (item.Tool >= m_Facts.size())
			m_Facts.resize(session.GetTools().size());

		std::optional<ToolFacts>& cached = m_Facts[item.Tool];
		if (!cached)
			cached = FactsOf(tool, context.Subagents, context.Prices);

		const ToolFacts& facts = *cached;
		const ImU32 colour = tool.IsError ? palette.Failed : palette.CategoryColor(facts.Category);
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		const float lineHeight = ImGui::GetTextLineHeight();

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		CategoryBar(colour, lineHeight);

		if (ImGui::Selectable("##step", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
			Toggle(m_OpenItems, key, row);

		// The right column: what came back, the tool's own fact, how long it took, and how it
		// ended. Pinned to the right edge; the summary gives way to it.
		std::string column;

		for (const std::string& piece : {facts.Size, facts.Detail, DurationText(tool)})
		{
			if (piece.empty())
				continue;

			if (!column.empty())
				column += "  ";

			column += piece;
		}

		const std::string status = !tool.HasResult ? std::string{} : tool.IsError ? ICON_MDI_CLOSE : ICON_MDI_CHECK;
		const float space = ImGui::GetStyle().ItemSpacing.x;
		const float rightWidth = ImGui::CalcTextSize(column.c_str()).x +
								 (status.empty() ? 0.0f : space + ImGui::CalcTextSize(status.c_str()).x);

		ImVec2 at(start.x + kBarGap, start.y);

		DrawPiece(at, colour, std::string{IconOf(facts.Category)} + " ");

		// The name and the summary are cut as one, so a long name gives way too rather than
		// running under the right column. The column itself never starts left of the row.
		const float columnX = std::max(right - rightWidth, at.x);

		DrawPiece(at, ink,
				  Fitted(tool.Name + "  " + SingleLineLabel(ToolSummary(tool), 400), columnX - space - at.x));

		at.x = columnX;
		DrawPiece(at, palette.TextDisabled, column);

		if (!status.empty())
		{
			at.x += space;
			DrawPiece(at, tool.IsError ? palette.Failed : ink, status);
		}

		if (!open)
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
			{
				ImGui::PushStyleColor(ImGuiCol_Text, line.starts_with("- ") ? palette.Failed : palette.Run);
				ImGui::TextWrapped("%s", line.c_str());
				ImGui::PopStyleColor();
			}
		}
		else
		{
			ImGui::TextWrapped("%s", Pretty(tool.InputJson).c_str());
		}

		if (tool.HasResult)
		{
			ImGui::Separator();

			// Coloured by the style rather than TextColored, which does not wrap: an error is
			// as long as any other result.
			if (tool.IsError)
				ImGui::PushStyleColor(ImGuiCol_Text, palette.Failed);

			ImGui::TextWrapped("%s", tool.Result.c_str());

			if (tool.IsError)
				ImGui::PopStyleColor();
		}

		ImGui::Unindent();
	}

	void ConversationView::DrawInjected(const TurnItem& item, std::size_t row, const ConversationContext& context)
	{
		const std::size_t turn = m_Rows[row].Turn;
		const std::pair<std::size_t, std::size_t> key{turn, m_Parts[turn][m_Rows[row].Part].Items.front()};
		const bool open = m_OpenItems.contains(key);
		const ImU32 colour = context.Palette.Injected;
		const float lineHeight = ImGui::GetTextLineHeight();

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		if (ImGui::Selectable("##injected", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
			Toggle(m_OpenItems, key, row);

		const std::string time = ClockTime(item.At);
		const float timeWidth = ImGui::CalcTextSize(time.c_str()).x;
		const float space = ImGui::GetStyle().ItemSpacing.x;

		ImVec2 at(start.x + kBarGap, start.y);

		DrawPiece(at, colour, std::string{ICON_MDI_BELL_OUTLINE} + " ");

		const float timeX = std::max(right - timeWidth, at.x);

		DrawPiece(at, colour,
				  Fitted(SingleLineLabel(item.Label.empty() ? item.Text : item.Label, 200), timeX - space - at.x));

		at.x = timeX;
		DrawPiece(at, colour, time);

		if (!open)
			return;

		ImGui::Indent();
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextWrapped("%s", item.Text.c_str());
		ImGui::PopStyleColor();
		ImGui::Unindent();
	}

	void ConversationView::DrawGap(std::chrono::milliseconds gap, const ConversationContext& context)
	{
		ImGui::Spacing();

		const std::string text = std::string{ICON_MDI_TIMER_SAND} + " " + FormatDuration(gap);
		const float width = ImGui::CalcTextSize(text.c_str()).x;

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
		ColouredText(context.Palette.TextDisabled, text);

		ImGui::Spacing();
	}

	void ConversationView::DrawMarkdown(const std::string& text, std::pair<std::size_t, std::size_t> key,
										const ConversationContext& context)
	{
		const float width = ImGui::GetContentRegionAvail().x - kBlockPadding;
		ImDrawList* draw = ImGui::GetWindowDrawList();

		MarkdownCache& cache = m_Markdown[key];

		if (cache.LaidWidth < 0.0f || cache.Text != text)
		{
			cache.Text = text;
			cache.Blocks = ParseMarkdown(text);
			cache.LaidWidth = -1.0f;
		}

		// Laid out again only when the width or the font size moved: placing the words measures
		// every one of them, too much for every visible reply every frame.
		const bool relayout = cache.LaidWidth != width || cache.LaidFontSize != ImGui::GetFontSize();

		if (relayout)
		{
			cache.Layouts.assign(cache.Blocks.size(), {});
			cache.LaidWidth = width;
			cache.LaidFontSize = ImGui::GetFontSize();
		}

		const std::vector<MarkdownBlock>& blocks = cache.Blocks;

		for (std::size_t index = 0; index < blocks.size(); ++index)
		{
			const MarkdownBlock& block = blocks[index];

			if (block.Kind == MarkdownKind::Code)
			{
				// Its own box and its own sideways scroll: code is never wrapped, and the
				// conversation itself has no horizontal scrollbar. The box is padded like a
				// window; its height is the code's lines in the monospace face it is drawn in,
				// plus that padding, plus room for the scrollbar when the code is wider than the
				// space inside the padding, so it never also needs a vertical one.
				const ImGuiStyle& style = ImGui::GetStyle();
				ImFont* mono = Core::ImGuiRenderer::GetMonoFont();
				const float lines = static_cast<float>(std::count(block.Code.begin(), block.Code.end(), '\n') + 1);

				ImGui::PushFont(mono, 0.0f);
				const float codeWidth = ImGui::CalcTextSize(block.Code.c_str(), block.Code.c_str() + block.Code.size()).x;
				float height = lines * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;
				ImGui::PopFont();

				if (codeWidth > width - style.WindowPadding.x * 2.0f)
					height += style.ScrollbarSize;

				// Identified by the block's place within the row, which the row's own id
				// qualifies: stable from frame to frame, so the box keeps its scroll.
				ImGui::PushID(static_cast<int>(index));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, context.Palette.CodeBlock);

				if (ImGui::BeginChild("##code", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
									  ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
				{
					ImGui::PushFont(mono, 0.0f);
					ImGui::TextUnformatted(block.Code.c_str(), block.Code.c_str() + block.Code.size());
					ImGui::PopFont();
				}

				// EndChild whether or not BeginChild returned true.
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::PopID();
				continue;
			}

			const float scale = block.Kind == MarkdownKind::Heading
									? (block.Level == 1 ? 1.35f : block.Level == 2 ? 1.2f : 1.1f)
									: 1.0f;

			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);

			const float indent = block.Kind == MarkdownKind::ListItem
									 ? ImGui::GetFontSize() * (1.0f + static_cast<float>(block.Level))
									 : 0.0f;
			const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			const bool bold = block.Kind == MarkdownKind::Heading;
			const float codePad = ImGui::GetFontSize() * 0.3f;

			if (block.Kind == MarkdownKind::ListItem)
				draw->AddText(ImVec2(origin.x + indent - ImGui::CalcTextSize(block.Marker.c_str()).x -
										 ImGui::GetFontSize() * 0.4f,
									 origin.y),
							  ink, block.Marker.c_str());

			const auto measure = [codePad](std::string_view word, SpanStyle style)
			{
				const float pad = style == SpanStyle::Code ? codePad : 0.0f;
				return ImGui::CalcTextSize(word.data(), word.data() + word.size()).x + pad;
			};

			if (relayout)
				cache.Layouts[index] = LayoutSpans(block.Spans, width - indent, measure);

			const std::vector<PlacedWord>& words = cache.Layouts[index];
			int lastLine = 0;

			for (const PlacedWord& word : words)
			{
				const SpanStyle style = block.Spans[word.Span].Style;
				const ImVec2 at(origin.x + indent + word.X, origin.y + static_cast<float>(word.Line) * lineHeight);
				const char* begin = word.Text.c_str();
				const char* end = begin + word.Text.size();

				if (style == SpanStyle::Code)
				{
					// The background covers the word, not the space placed after it.
					const char* trimmed = end;
					while (trimmed > begin && trimmed[-1] == ' ')
						--trimmed;

					const ImVec2 size = ImGui::CalcTextSize(begin, trimmed);
					draw->AddRectFilled(at, ImVec2(at.x + size.x + codePad, at.y + ImGui::GetTextLineHeight()),
										context.Palette.CodeBlock, 3.0f);
					draw->AddText(ImVec2(at.x + codePad * 0.5f, at.y), ink, begin, trimmed);
				}
				else
				{
					draw->AddText(at, ink, begin, end);

					// There is one text face; bold is the same glyphs drawn twice, a pixel apart.
					if (bold || style == SpanStyle::Bold)
						draw->AddText(ImVec2(at.x + 1.0f, at.y), ink, begin, end);
				}

				lastLine = word.Line;
			}

			ImGui::PopFont();

			// The layout drew through the draw list; the cursor has to be moved past it with an
			// item, or ImGui asserts on the next SetCursorPos.
			ImGui::Dummy(ImVec2(width, static_cast<float>(words.empty() ? 1 : lastLine + 1) * lineHeight));
		}
	}

	void ConversationView::DrawRow(const Session& session, std::size_t index, const ConversationContext& context,
								   Core::EventBus& events)
	{
		const Row& row = m_Rows[index];
		const Turn& turn = session.GetTurns()[row.Turn];

		// Identified by what the row shows rather than by its index, which moves whenever a run
		// of steps above it opens: a code box inside keeps its scroll across that.
		const std::string id = std::format("{}/{}/{}/{}", static_cast<int>(row.Kind), row.Turn, row.Part, row.Item);
		ImGui::PushID(id.c_str());

		switch (row.Kind)
		{
		case RowKind::User:
			DrawUser(turn.Prompt, turn.StartedAt, context);
			break;
		case RowKind::Gap:
			DrawGap(row.Gap, context);
			break;
		case RowKind::Part:
		{
			const TurnPart& part = m_Parts[row.Turn][row.Part];

			switch (part.Kind)
			{
			case PartKind::Reply:
				DrawReply(turn, part, {row.Turn, row.Part}, context);
				break;
			case PartKind::Steps:
				DrawStepsHeader(session, turn, part, index, context);
				break;
			case PartKind::Injected:
				DrawInjected(turn.Items[part.Items.front()], index, context);
				break;
			case PartKind::Queued:
			{
				const TurnItem& item = turn.Items[part.Items.front()];
				DrawUser(item.Text, item.At, context);
				break;
			}
			}
			break;
		}
		case RowKind::Step:
			ImGui::Indent();
			DrawStep(session, turn.Items[row.Item], index, context, events);
			ImGui::Unindent();
			break;
		}

		ImGui::PopID();
	}

	void ConversationView::Draw(const Session& session, const ConversationContext& context, Core::EventBus& events)
	{
		// No horizontal scrollbar: everything wraps to the window, and a code block scrolls
		// sideways in a box of its own.
		// Padded like a window, so the blocks and the right-aligned times keep the same distance
		// from the scrollbar as from the left edge.
		if (!ImGui::BeginChild("##conversation", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
							   ImGuiWindowFlags_None))
		{
			ImGui::EndChild();
			return;
		}

		// A newer subagent map changes what an Agent call's facts say.
		if (context.SubagentsVersion != m_FactsSubagentsVersion)
		{
			for (std::optional<ToolFacts>& facts : m_Facts)
			{
				if (facts && facts->Category == ToolCategory::Agent)
					facts.reset();
			}

			m_FactsSubagentsVersion = context.SubagentsVersion;
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

			DrawRow(session, index, context, events);

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
