#include "Sessions/Conversation/ConversationView.h"

#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Base/Base.h"
#include "Core/Base/Strings.h"

#include "Sessions/Requests/RequestRow.h"
#include "Sessions/SessionDrawing.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/Lists/Chevron.h"
#include "UI/TypographicMarks.h"
#include "UI/FittedText.h"
#include "Workspace/Workspace.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/IconButton.h"
#include "Core/ImGui/FontSet.h"

#include <IconsLucide.h>

#include <nlohmann/json.hpp>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <map>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr float kBlockPadding = 8.0f;
		constexpr float kBarGap = 8.0f;

		constexpr int kJsonIndent = 2;
		constexpr std::size_t kToolSummaryMaxBytes = 400;
		constexpr std::size_t kInjectedLabelMaxBytes = 200;
		constexpr std::size_t kNamedToolCount = 3;

		constexpr float kLinesAroundText = 2.0f;
		constexpr float kMarkdownHeightFactor = 1.1f;
		constexpr float kDividerSpacings = 2.0f;
		constexpr float kSmallestSelectableWidth = 1.0f;
		constexpr float kAtEndTolerance = 1.0f;

		constexpr float kLevelOneHeadingScale = 1.35f;
		constexpr float kLevelTwoHeadingScale = 1.2f;
		constexpr float kLowerHeadingScale = 1.1f;
		constexpr float kCodePaddingPerFontSize = 0.3f;
		constexpr float kMarkerGapPerFontSize = 0.4f;
		constexpr float kCodeBackgroundRounding = 3.0f;
		constexpr float kBoldOffset = 1.0f;

		const ImVec2 kNoSize(0.0f, 0.0f);

		constexpr bool kNotSelected = false;
		constexpr bool kNotMeasured = false;

		float WrappedTextHeight(const std::string& text, float width)
		{
			const bool hideTextAfterDoubleHash = false;
			return ImGui::CalcTextSize(text.c_str(), nullptr, hideTextAfterDoubleHash, width).y;
		}

		std::string Pretty(const std::string& json)
		{
			const bool allowExceptions = false;
			const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, allowExceptions);
			const bool escapeNonAscii = false;
			return parsed.is_discarded()
					   ? json
					   : parsed.dump(kJsonIndent, ' ', escapeNonAscii, nlohmann::json::error_handler_t::replace);
		}

		std::string DurationText(const ToolCall& tool)
		{
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> startedAt =
				ParseTimestamp(tool.StartedAt);
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> finishedAt =
				ParseTimestamp(tool.FinishedAt);

			if (!startedAt || !finishedAt)
			{
				return {};
			}

			const std::chrono::milliseconds elapsed = *finishedAt - *startedAt;
			if (elapsed < std::chrono::seconds{1})
			{
				return std::to_string(elapsed.count()) + "ms";
			}

			return FormatDuration(elapsed);
		}

		struct Block
		{
			ImVec2 Start;
			float Width = 0.0f;
		};

		Block BeginBlock()
		{
			ImGui::Spacing();

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const int contentChannel = 1;
			drawList->ChannelsSplit(2);
			drawList->ChannelsSetCurrent(contentChannel);

			const Block block{ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x};

			ImGui::Dummy(kNoSize);
			ImGui::Indent(Core::Scaled(kBlockPadding));
			return block;
		}

		void EndBlock(const Block& block, ImU32 colour)
		{
			ImGui::Unindent(Core::Scaled(kBlockPadding));

			const float bottom = ImGui::GetCursorScreenPos().y;
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			const int backgroundChannel = 0;
			drawList->ChannelsSetCurrent(backgroundChannel);
			drawList->AddRectFilled(block.Start, ImVec2(block.Start.x + block.Width, bottom), colour,
									ImGui::GetStyle().FrameRounding);
			drawList->ChannelsMerge();
		}

		struct MeasuredHeight
		{
			float Height = 0.0f;
			bool Measured = false;
		};

		bool IsCodeStyle(SpanStyle style)
		{
			return style == SpanStyle::Code || style == SpanStyle::BoldCode;
		}

		float MeasuredWordWidth(float codePadding, std::string_view word, SpanStyle style)
		{
			const float padding = IsCodeStyle(style) ? codePadding : 0.0f;
			return ImGui::CalcTextSize(word.data(), word.data() + word.size()).x + padding;
		}

		const char* TextEndWithoutTrailingSpaces(const PlacedWord& word)
		{
			const char* textBegin = word.Text.c_str();
			const char* trimmedEnd = textBegin + word.Text.size();
			while (trimmedEnd > textBegin && trimmedEnd[-1] == ' ')
			{
				--trimmedEnd;
			}
			return trimmedEnd;
		}

		ImVec2 WordPosition(const PlacedWord& word, const ImVec2& origin, float indent, float lineHeight)
		{
			return ImVec2(origin.x + indent + word.X, origin.y + static_cast<float>(word.Line) * lineHeight);
		}

		float TextScaleOf(const MarkdownBlock& block)
		{
			if (block.Kind != MarkdownKind::Heading)
			{
				return 1.0f;
			}

			if (block.Level == 1)
			{
				return kLevelOneHeadingScale;
			}

			return block.Level == 2 ? kLevelTwoHeadingScale : kLowerHeadingScale;
		}
	}

	ConversationView::RowIdentity ConversationView::IdentityOf(const Row& row)
	{
		return RowIdentity{static_cast<int>(row.Kind), row.TurnIndex, row.PartIndex, row.ItemIndex,
						   row.WasOpenWhenBuilt};
	}

	void ConversationView::PushGap(std::vector<Row>& rows, std::optional<std::chrono::milliseconds> pause,
								   std::size_t turnIndex, std::size_t partIndex)
	{
		if (pause)
		{
			rows.push_back({.Kind = RowKind::Gap, .TurnIndex = turnIndex, .PartIndex = partIndex, .Pause = *pause});
		}
	}

	void ConversationView::PushGapTo(std::vector<Row>& rows, const std::string& latestTime,
									 std::string_view nextTime, std::size_t turnIndex, std::size_t partIndex)
	{
		if (latestTime.empty() || nextTime.empty())
		{
			return;
		}

		PushGap(rows, GapBetween(latestTime, nextTime), turnIndex, partIndex);
	}

	void ConversationView::PushGapBeforeItem(std::vector<Row>& rows, const std::string& latestTime,
											 const TurnItem& item, std::size_t turnIndex, std::size_t partIndex)
	{
		if (latestTime.empty() || item.At.empty())
		{
			return;
		}

		PushGap(rows, GapBefore(latestTime, item), turnIndex, partIndex);
	}

	void ConversationView::Toggle(std::set<std::pair<std::size_t, std::size_t>>& openKeys,
								  std::pair<std::size_t, std::size_t> key, std::size_t rowIndex)
	{
		if (openKeys.contains(key))
		{
			openKeys.erase(key);
		}
		else
		{
			openKeys.insert(key);
		}

		m_Measured[rowIndex] = false;
		m_OffsetsDirty = true;
	}

	void ConversationView::Rebuild(const Session& session, float width)
	{
		const bool sameSession = m_BuiltGeneration != kNeverBuilt && session.GetGeneration() == m_BuiltGeneration;

		if (!sameSession)
		{
			m_OpenParts.clear();
			m_OpenItems.clear();
			m_OpenUsage.clear();
		}

		using KeptHeights = std::map<RowIdentity, MeasuredHeight>;
		KeptHeights keptHeights;

		if (sameSession && width == m_BuiltWidth)
		{
			for (std::size_t rowIndex = 0; rowIndex < m_Rows.size(); ++rowIndex)
			{
				keptHeights.emplace(IdentityOf(m_Rows[rowIndex]),
									MeasuredHeight{m_Heights[rowIndex], m_Measured[rowIndex]});
			}
		}

		const std::vector<Turn>& turns = session.GetTurns();
		m_Parts.assign(turns.size(), {});

		m_Facts.Reset(session.GetTools().size());

		std::vector<Row> rows;
		std::string latestTime;

		for (std::size_t turnIndex = 0; turnIndex < turns.size(); ++turnIndex)
		{
			const Turn& turn = turns[turnIndex];

			if (!turn.Prompt.empty())
			{
				PushGapTo(rows, latestTime, turn.StartedAt, turnIndex, kBeforeUser);
				rows.push_back({.Kind = RowKind::User, .TurnIndex = turnIndex});
			}

			if (!turn.StartedAt.empty())
			{
				latestTime = turn.StartedAt;
			}

			m_Parts[turnIndex] = SplitTurn(turn, session.GetTools());

			for (std::size_t partIndex = 0; partIndex < m_Parts[turnIndex].size(); ++partIndex)
			{
				const TurnPart& turnPart = m_Parts[turnIndex][partIndex];

				if (turnPart.Items.empty())
				{
					continue;
				}

				PushGapBeforeItem(rows, latestTime, turn.Items[turnPart.Items.front()], turnIndex, partIndex);

				const bool isOpen = turnPart.Kind == PartKind::Steps && m_OpenParts.contains({turnIndex, partIndex});
				rows.push_back({.Kind = RowKind::Part,
								.TurnIndex = turnIndex,
								.PartIndex = partIndex,
								.WasOpenWhenBuilt = isOpen});

				if (isOpen)
				{
					for (const std::size_t itemIndex : turnPart.Items)
					{
						rows.push_back({.Kind = RowKind::Step,
										.TurnIndex = turnIndex,
										.PartIndex = partIndex,
										.ItemIndex = itemIndex});
					}

					if (turnPart.Requests > 0)
					{
						rows.push_back({.Kind = RowKind::RunUsage, .TurnIndex = turnIndex, .PartIndex = partIndex});
					}
				}

				if (std::string activityTime = LastActivity(turn, turnPart, session.GetTools()); !activityTime.empty())
				{
					latestTime = std::move(activityTime);
				}
			}
		}

		m_Heights.assign(rows.size(), 0.0f);
		m_Measured.assign(rows.size(), kNotMeasured);

		for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
		{
			const KeptHeights::const_iterator kept = keptHeights.find(IdentityOf(rows[rowIndex]));
			if (kept != keptHeights.end())
			{
				[[maybe_unused]] const auto& [keptIdentity, keptHeight] = *kept;
				m_Heights[rowIndex] = keptHeight.Height;
				m_Measured[rowIndex] = keptHeight.Measured;
			}
			else
			{
				m_Heights[rowIndex] = Estimate(session, rows[rowIndex], width);
			}
		}

		m_Markdown.clear();

		m_Rows = std::move(rows);
		m_BuiltVersion = session.GetVersion();
		m_BuiltGeneration = session.GetGeneration();
		m_BuiltWidth = width;
		m_OffsetsDirty = true;
	}

	float ConversationView::Estimate(const Session& session, const Row& row, float width) const
	{
		const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
		const float itemSpacingY = ImGui::GetStyle().ItemSpacing.y;
		const Turn& turn = session.GetTurns()[row.TurnIndex];

		switch (row.Kind)
		{
		case RowKind::User:
			return WrappedTextHeight(turn.Prompt, width) + kLinesAroundText * lineHeight;
		case RowKind::Gap:
			return lineHeight + kDividerSpacings * itemSpacingY;
		case RowKind::Step:
			return lineHeight;
		case RowKind::Part:
		case RowKind::RunUsage:
			break;
		}

		const TurnPart& part = m_Parts[row.TurnIndex][row.PartIndex];

		const float requestLinesHeight = m_OpenUsage.contains({row.TurnIndex, row.PartIndex})
											 ? lineHeight * static_cast<float>(part.RequestKeys.size())
											 : 0.0f;

		if (row.Kind == RowKind::RunUsage)
		{
			return lineHeight + requestLinesHeight;
		}

		switch (part.Kind)
		{
		case PartKind::Reply:
		{
			const std::string& text = turn.Items[part.Items.front()].Text;
			return WrappedTextHeight(text, width) * kMarkdownHeightFactor +
				   kLinesAroundText * lineHeight + requestLinesHeight;
		}
		case PartKind::Steps:
			if (row.WasOpenWhenBuilt)
			{
				return lineHeight;
			}

			return (part.Requests > 0 ? kLinesAroundText * lineHeight : lineHeight) + requestLinesHeight;
		case PartKind::Injected:
			return lineHeight;
		case PartKind::Queued:
		{
			const std::string& text = turn.Items[part.Items.front()].Text;
			return WrappedTextHeight(text, width) + kLinesAroundText * lineHeight;
		}
		case PartKind::Compaction:
			return lineHeight + kDividerSpacings * itemSpacingY;
		}

		return lineHeight;
	}

	void ConversationView::DrawHeaderTime(const std::string& timestamp, const ConversationContext& context)
	{
		const std::string formattedTime = m_Clock.Format(timestamp, ClockStyle::MonthDayHoursMinutes);

		if (formattedTime.empty())
		{
			return;
		}

		ImGui::SameLine();
		ColouredText(context.Palette.TextDisabled, formattedTime);
		ImGui::SetItemTooltip("%s", m_Clock.Format(timestamp, ClockStyle::DateTimeWeekday).c_str());
	}

	void ConversationView::DrawUser(const std::string& text, const std::string& timestamp,
									const ConversationContext& context)
	{
		const Block block = BeginBlock();

		ColouredText(context.Palette.Accent, "User");
		DrawHeaderTime(timestamp, context);

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Core::Scaled(kBlockPadding));
		ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
		ImGui::PopTextWrapPos();

		EndBlock(block, context.Palette.UserBlock);
	}

	void ConversationView::DrawUsage(const TurnPart& part, std::size_t rowIndex, float width,
									 const ConversationContext& context)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[rowIndex].TurnIndex, m_Rows[rowIndex].PartIndex};
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImVec2 start = ImGui::GetCursorScreenPos();

		const float selectableWidth = std::max(width, kSmallestSelectableWidth);
		const float right = start.x + selectableWidth;

		if (ImGui::Selectable("##usage", kNotSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(selectableWidth, lineHeight)))
		{
			Toggle(m_OpenUsage, key, rowIndex);
		}

		const bool isOpen = m_OpenUsage.contains(key);
		ImVec2 cursor = start;
		DrawPiece(cursor, context.Palette.TextDisabled,
				  FitToWidth(std::string{ChevronGlyph(isOpen)} + " ", right - cursor.x));
		DrawPiece(cursor, context.Palette.TextDisabled,
				  FitToWidth(UsageLine(part.Usage, part.Requests, context.Prices), right - cursor.x));

		if (!isOpen || context.Timeline == nullptr)
		{
			return;
		}

		const std::vector<ApiRequest>& requests = context.Timeline->GetRequests();
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
		const float buttonWidth = Core::IconButtonSize(Core::IconButtonFit::Line);
		const bool canOpenRequest = static_cast<bool>(context.OpenRequest);

		ImGui::Indent();

		for (const std::string& requestKey : part.RequestKeys)
		{
			const std::optional<std::size_t> requestPosition = context.Timeline->IndexOf(requestKey);
			if (!requestPosition)
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(*requestPosition));

			const ImVec2 linePosition = ImGui::GetCursorScreenPos();
			const float buttonX = std::max(right - buttonWidth, linePosition.x);
			const float lineRight = canOpenRequest ? buttonX - itemSpacingX : right;

			DrawRequestLine(linePosition, lineRight, requests[*requestPosition], context.Prices, context.Palette,
							m_Clock);

			if (canOpenRequest)
			{
				ImGui::SetCursorScreenPos(ImVec2(buttonX, linePosition.y));

				constexpr bool kNotEngaged = false;

				if (Core::DrawIconButton("##openRequest", ICON_LC_ARROW_RIGHT, kNotEngaged, "Show in the Requests tab",
										 {.Fit = Core::IconButtonFit::Line}))
				{
					context.OpenRequest(*requestPosition);
				}
			}
			else
			{
				ImGui::Dummy(ImVec2(0.0f, lineHeight));
			}

			ImGui::PopID();
		}

		ImGui::Unindent();
	}

	void ConversationView::DrawReply(const Turn& turn, const TurnPart& part, std::size_t rowIndex,
									 const ConversationContext& context)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[rowIndex].TurnIndex, m_Rows[rowIndex].PartIndex};
		const TurnItem& item = turn.Items[part.Items.front()];
		const Block block = BeginBlock();

		ColouredText(context.Palette.Accent, "Claude");
		DrawHeaderTime(item.At, context);

		if (part.Requests > 0)
		{
			ImGui::SameLine();
			DrawUsage(part, rowIndex, ImGui::GetContentRegionAvail().x - Core::Scaled(kBlockPadding), context);
		}

		DrawMarkdown(item.Text, key, context);

		EndBlock(block, context.Palette.ClaudeBlock);
	}

	void ConversationView::DrawStepsHeader(const Session& session, const Turn& turn, const TurnPart& part,
										   std::size_t rowIndex, const ConversationContext& context)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[rowIndex].TurnIndex, m_Rows[rowIndex].PartIndex};
		const bool isOpen = m_OpenParts.contains(key);
		const float lineHeight = ImGui::GetTextLineHeight();
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		ImU32 barColour = context.Palette.Other;
		bool allToolsFinished = true;
		bool hasTool = false;

		for (const std::size_t itemIndex : part.Items)
		{
			const TurnItem& item = turn.Items[itemIndex];

			if (item.Kind != ItemKind::Tool)
			{
				continue;
			}

			const ToolCall& tool = session.GetTools()[item.Tool];

			if (!hasTool)
			{
				barColour = context.Palette.CategoryColor(CategoryOf(tool.Name));
			}

			hasTool = true;
			allToolsFinished = allToolsFinished && tool.HasResult;
		}

		if (part.Failures > 0)
		{
			barColour = context.Palette.Failed;
		}

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		CategoryBar(barColour, lineHeight);

		if (ImGui::Selectable("##steps", kNotSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
		{
			Toggle(m_OpenParts, key, rowIndex);

			m_BuiltVersion = kNeverBuilt;
		}

		const std::size_t stepCount = part.Items.size();
		std::string headerText = std::string{ChevronGlyph(isOpen)} + " " +
								 Core::Counted(stepCount, "step");

		const std::size_t namedToolTotal = std::min(part.ToolCounts.size(), kNamedToolCount);
		std::vector<std::string> namedTools;

		for (std::size_t toolIndex = 0; toolIndex < namedToolTotal; ++toolIndex)
		{
			const auto& [toolName, callCount] = part.ToolCounts[toolIndex];
			namedTools.push_back(toolName + " " + std::string{kMultiplicationSign} + std::to_string(callCount));
		}

		if (!namedTools.empty())
		{
			headerText += std::string{kMiddleDotSeparator} + Core::JoinedWith(namedTools, ", ");
		}

		if (part.ToolCounts.size() > kNamedToolCount)
		{
			headerText += ", + " + std::to_string(part.ToolCounts.size() - kNamedToolCount) + " more";
		}

		const std::string durationText =
			part.Duration.count() > 0 ? std::string{kMiddleDotSeparator} + FormatDuration(part.Duration) : std::string{};
		const bool showStatus = hasTool && allToolsFinished;

		std::string statusText;
		if (showStatus)
		{
			statusText = part.Failures > 0 ? std::string{ICON_LC_X} + " " + std::to_string(part.Failures)
										   : std::string{ICON_LC_CHECK};
		}

		const std::string statusSeparator = showStatus ? std::string{kMiddleDotSeparator} : std::string{};

		const float trailingWidth = ImGui::CalcTextSize((durationText + statusSeparator + statusText).c_str()).x;
		ImVec2 cursor(start.x + Core::Scaled(kBarGap), start.y);

		DrawPiece(cursor, ink, FitToWidth(headerText, right - cursor.x - trailingWidth));
		DrawPiece(cursor, context.Palette.TextDisabled, durationText + statusSeparator);
		DrawPiece(cursor, part.Failures > 0 ? context.Palette.Failed : ink, statusText);

		if (part.Requests > 0 && !isOpen)
		{
			ImGui::Indent(Core::Scaled(kBarGap));
			DrawUsage(part, rowIndex, ImGui::GetContentRegionAvail().x, context);
			ImGui::Unindent(Core::Scaled(kBarGap));
		}
	}

	void ConversationView::DrawRunUsage(const TurnPart& part, std::size_t rowIndex, const ConversationContext& context)
	{
		ImGui::Indent();
		ImGui::Indent(Core::Scaled(kBarGap));
		DrawUsage(part, rowIndex, ImGui::GetContentRegionAvail().x, context);
		ImGui::Unindent(Core::Scaled(kBarGap));
		ImGui::Unindent();
	}

	void ConversationView::DrawStep(const Session& session, const TurnItem& item, std::size_t rowIndex,
									const ConversationContext& context, Core::EventBus& events)
	{
		const std::pair<std::size_t, std::size_t> key{m_Rows[rowIndex].TurnIndex, m_Rows[rowIndex].ItemIndex};
		const bool isOpen = m_OpenItems.contains(key);
		const SessionPalette& palette = context.Palette;

		switch (item.Kind)
		{
		case ItemKind::Tool:
			break;
		case ItemKind::Thinking:
		{
			if (item.Text.empty())
			{
				return;
			}

			const std::string label =
				std::string{ChevronGlyph(isOpen)} + " thinking##thinking";

			ImGui::PushStyleColor(ImGuiCol_Text, palette.TextDisabled);

			if (ImGui::Selectable(label.c_str()))
			{
				Toggle(m_OpenItems, key, rowIndex);
			}

			if (isOpen)
			{
				ImGui::TextWrapped("%s", item.Text.c_str());
			}

			ImGui::PopStyleColor();
			return;
		}
		case ItemKind::Other:
			ColouredText(palette.TextDisabled, "<" + item.Text + ">");
			return;
		case ItemKind::Text:
		case ItemKind::Injected:
		case ItemKind::Queued:
		case ItemKind::Compaction:
			ImGui::TextWrapped("%s", item.Text.c_str());
			return;
		}

		const ToolCall& tool = session.GetTools()[item.Tool];

		const ToolFacts& facts = m_Facts.FactsOf(item.Tool, tool, context.Subagents, context.SubagentsVersion, context.Prices);
		const ImU32 colour = tool.IsError ? palette.Failed : palette.CategoryColor(facts.Category);
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		const float lineHeight = ImGui::GetTextLineHeight();

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		CategoryBar(colour, lineHeight);

		if (ImGui::Selectable("##step", kNotSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
		{
			Toggle(m_OpenItems, key, rowIndex);
		}

		std::string column;

		for (const std::string& piece : {facts.Size, facts.Detail, DurationText(tool)})
		{
			Core::AppendSeparatedIfNotEmpty(column, "  ", piece);
		}

		std::string statusText;
		if (tool.HasResult)
		{
			statusText = tool.IsError ? ICON_LC_X : ICON_LC_CHECK;
		}

		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
		const float statusWidth = statusText.empty() ? 0.0f : itemSpacingX + ImGui::CalcTextSize(statusText.c_str()).x;
		const float rightWidth = ImGui::CalcTextSize(column.c_str()).x + statusWidth;

		ImVec2 cursor(start.x + Core::Scaled(kBarGap), start.y);

		DrawPiece(cursor, colour, std::string{IconOf(facts.Category)} + " ");

		const float columnX = std::max(right - rightWidth, cursor.x);

		DrawPiece(cursor, ink,
				  FitToWidth(tool.Name + "  " + SingleLineLabel(ToolSummary(tool), kToolSummaryMaxBytes),
							 columnX - itemSpacingX - cursor.x));

		cursor.x = columnX;
		DrawPiece(cursor, palette.TextDisabled, column);

		if (!statusText.empty())
		{
			cursor.x += itemSpacingX;
			DrawPiece(cursor, tool.IsError ? palette.Failed : ink, statusText);
		}

		if (!isOpen)
		{
			return;
		}

		ImGui::Indent();

		if (const std::optional<std::filesystem::path> filePath = ToolFilePath(tool))
		{
			const std::string pathText = Core::ToUtf8(*filePath);

			if (Core::SmallTextButton(pathText.c_str()))
			{
				const bool openPermanently = false;
				events.Publish(OpenFileRequested{*filePath, openPermanently});
			}
		}

		if (context.OpenAgent && context.Links != nullptr && IsAgentCall(tool) && context.Links->contains(tool.Id))
		{
			if (Core::SmallTextButton("Open conversation " ICON_LC_ARROW_RIGHT))
			{
				context.OpenAgent(tool.Id);
			}
		}

		Core::PushCodeFont();

		if (const std::vector<std::string> editLines = EditLines(tool); !editLines.empty())
		{
			for (const std::string& editLine : editLines)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, editLine.starts_with("- ") ? palette.Failed : palette.Run);
				ImGui::TextWrapped("%s", editLine.c_str());
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

			if (tool.IsError)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, palette.Failed);
			}

			ImGui::TextWrapped("%s", tool.Result.c_str());

			if (tool.IsError)
			{
				ImGui::PopStyleColor();
			}
		}

		ImGui::PopFont();

		ImGui::Unindent();
	}

	void ConversationView::DrawInjected(const TurnItem& item, std::size_t rowIndex, const ConversationContext& context)
	{
		const std::size_t turnIndex = m_Rows[rowIndex].TurnIndex;
		const std::pair<std::size_t, std::size_t> key{turnIndex,
													  m_Parts[turnIndex][m_Rows[rowIndex].PartIndex].Items.front()};
		const bool isOpen = m_OpenItems.contains(key);
		const ImU32 colour = context.Palette.Injected;
		const float lineHeight = ImGui::GetTextLineHeight();

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float right = start.x + ImGui::GetContentRegionAvail().x;

		if (ImGui::Selectable("##injected", kNotSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, lineHeight)))
		{
			Toggle(m_OpenItems, key, rowIndex);
		}

		const std::string formattedTime = m_Clock.Format(item.At, ClockStyle::MonthDayHoursMinutes);
		const float timeWidth = ImGui::CalcTextSize(formattedTime.c_str()).x;
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;

		ImVec2 cursor(start.x + Core::Scaled(kBarGap), start.y);

		DrawPiece(cursor, colour, std::string{ICON_LC_BELL} + " ");

		const float timeX = std::max(right - timeWidth, cursor.x);

		DrawPiece(cursor, colour,
				  FitToWidth(SingleLineLabel(item.Label.empty() ? item.Text : item.Label, kInjectedLabelMaxBytes),
							 timeX - itemSpacingX - cursor.x));

		cursor.x = timeX;
		DrawPiece(cursor, colour, formattedTime);

		if (!isOpen)
		{
			return;
		}

		ImGui::Indent();
		ImGui::PushStyleColor(ImGuiCol_Text, colour);
		ImGui::TextWrapped("%s", item.Text.c_str());
		ImGui::PopStyleColor();
		ImGui::Unindent();
	}

	void ConversationView::DrawCompaction(const TurnItem& item, std::size_t rowIndex,
										  const ConversationContext& context)
	{
		const std::size_t turnIndex = m_Rows[rowIndex].TurnIndex;
		const std::pair<std::size_t, std::size_t> key{turnIndex,
													  m_Parts[turnIndex][m_Rows[rowIndex].PartIndex].Items.front()};
		const bool hasSummary = !item.Text.empty();
		const bool isOpen = hasSummary && m_OpenItems.contains(key);
		const ImU32 colour = context.Palette.Injected;
		const float lineHeight = ImGui::GetTextLineHeight();

		ImGui::Spacing();

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;

		if (hasSummary)
		{
			if (ImGui::Selectable("##compaction", kNotSelected, ImGuiSelectableFlags_None, ImVec2(width, lineHeight)))
			{
				Toggle(m_OpenItems, key, rowIndex);
			}
		}
		else
		{
			ImGui::Dummy(ImVec2(width, lineHeight));
		}

		if (item.Compaction.PreTokens && item.Compaction.PostTokens)
		{
			ImGui::SetItemTooltip("%s → %s tokens", FormatTokensExact(*item.Compaction.PreTokens).c_str(),
								  FormatTokensExact(*item.Compaction.PostTokens).c_str());
		}

		const std::string compactionTime = m_Clock.Format(item.At, ClockStyle::MonthDayHoursMinutes);
		const std::string label =
			FitToWidth(std::string{ICON_LC_CHEVRONS_DOWN_UP} + " " + CompactionLabel(item.Compaction, compactionTime),
					   width);
		const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
		const float labelX = start.x + std::max(0.0f, (width - labelWidth) * 0.5f);
		const float middleY = start.y + lineHeight * 0.5f;
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		if (labelX - itemSpacingX > start.x)
		{
			drawList->AddLine(ImVec2(start.x, middleY), ImVec2(labelX - itemSpacingX, middleY), colour);
		}
		if (labelX + labelWidth + itemSpacingX < start.x + width)
		{
			drawList->AddLine(ImVec2(labelX + labelWidth + itemSpacingX, middleY), ImVec2(start.x + width, middleY),
							  colour);
		}

		ImVec2 cursor(labelX, start.y);
		DrawPiece(cursor, colour, label);

		if (isOpen)
		{
			ImGui::Indent();
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextWrapped("%s", item.Text.c_str());
			ImGui::PopStyleColor();
			ImGui::Unindent();
		}

		ImGui::Spacing();
	}

	void ConversationView::DrawGap(std::chrono::milliseconds gap, const ConversationContext& context)
	{
		ImGui::Spacing();

		const std::string text = std::string{ICON_LC_HOURGLASS} + " " + FormatDuration(gap);
		const float width = ImGui::CalcTextSize(text.c_str()).x;

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
		ColouredText(context.Palette.TextDisabled, text);

		ImGui::Spacing();
	}

	void ConversationView::DrawMarkdown(const std::string& text, std::pair<std::size_t, std::size_t> key,
										const ConversationContext& context)
	{
		const float width = std::max(0.0f, ImGui::GetContentRegionAvail().x - Core::Scaled(kBlockPadding));
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		MarkdownCache& cache = m_Markdown[key];

		if (!cache.Parsed || cache.Text != text)
		{
			cache.Parsed = true;
			cache.Text = text;
			cache.Blocks = ParseMarkdown(text);
			cache.LaidWidth.reset();
		}

		const bool widthChanged = !cache.LaidWidth || *cache.LaidWidth != width;
		const bool blockCountChanged = cache.Layouts.size() != cache.Blocks.size();
		const bool fontSizeChanged = cache.LaidFontSize != ImGui::GetFontSize();
		const bool needsLayout = widthChanged || blockCountChanged || fontSizeChanged;

		if (needsLayout)
		{
			cache.Layouts.assign(cache.Blocks.size(), {});
			cache.LaidWidth = width;
			cache.LaidFontSize = ImGui::GetFontSize();
		}

		const std::vector<MarkdownBlock>& blocks = cache.Blocks;

		for (std::size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex)
		{
			const MarkdownBlock& block = blocks[blockIndex];

			if (block.Kind == MarkdownKind::Code)
			{
				const ImGuiStyle& style = ImGui::GetStyle();
				const float lineCount =
					static_cast<float>(std::count(block.Code.begin(), block.Code.end(), '\n') + 1);

				Core::PushCodeFont();
				const float codeWidth = ImGui::CalcTextSize(block.Code.c_str(), block.Code.c_str() + block.Code.size()).x;
				float height = lineCount * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;
				ImGui::PopFont();

				if (codeWidth > width - style.WindowPadding.x * 2.0f)
				{
					height += style.ScrollbarSize;
				}

				ImGui::PushID(static_cast<int>(blockIndex));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, context.Palette.CodeBlock);

				if (ImGui::BeginChild("##code", ImVec2(width, height), ImGuiChildFlags_AlwaysUseWindowPadding,
									  ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
				{
					Core::PushCodeFont();
					ImGui::TextUnformatted(block.Code.c_str(), block.Code.c_str() + block.Code.size());
					ImGui::PopFont();
				}

				// WARNING: EndChild must be called whether or not BeginChild returned true.
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::PopID();
				continue;
			}

			ImFont* const currentFont = nullptr;
			ImGui::PushFont(currentFont, ImGui::GetStyle().FontSizeBase * TextScaleOf(block));

			const float indent = block.Kind == MarkdownKind::ListItem
									 ? ImGui::GetFontSize() * (1.0f + static_cast<float>(block.Level))
									 : 0.0f;
			const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
			const bool isHeading = block.Kind == MarkdownKind::Heading;
			const float codePadding = ImGui::GetFontSize() * kCodePaddingPerFontSize;

			if (block.Kind == MarkdownKind::ListItem)
			{
				const float markerWidth = ImGui::CalcTextSize(block.Marker.c_str()).x;
				const float markerGap = ImGui::GetFontSize() * kMarkerGapPerFontSize;
				drawList->AddText(ImVec2(origin.x + indent - markerWidth - markerGap, origin.y), ink,
								  block.Marker.c_str());
			}

			const std::function<float(std::string_view, SpanStyle)> measure =
				std::bind_front(MeasuredWordWidth, codePadding);

			if (needsLayout)
			{
				cache.Layouts[blockIndex] = LayoutSpans(block.Spans, width - indent, measure);
			}

			const std::vector<PlacedWord>& words = cache.Layouts[blockIndex];
			int lastLineIndex = 0;

			for (std::size_t wordIndex = 0; wordIndex < words.size(); ++wordIndex)
			{
				const PlacedWord& word = words[wordIndex];
				const SpanStyle style = block.Spans[word.Span].Style;
				const ImVec2 position = WordPosition(word, origin, indent, lineHeight);
				const char* wordBegin = word.Text.c_str();
				const char* wordEnd = wordBegin + word.Text.size();

				if (IsCodeStyle(style))
				{
					std::size_t lastWordOfRun = wordIndex;
					while (lastWordOfRun + 1 < words.size() && words[lastWordOfRun + 1].Span == word.Span &&
						   words[lastWordOfRun + 1].Line == word.Line)
					{
						++lastWordOfRun;
					}

					const PlacedWord& runTail = words[lastWordOfRun];
					const ImVec2 runTailPosition = WordPosition(runTail, origin, indent, lineHeight);
					const ImVec2 runTailSize =
						ImGui::CalcTextSize(runTail.Text.c_str(), TextEndWithoutTrailingSpaces(runTail));
					drawList->AddRectFilled(
						position,
						ImVec2(runTailPosition.x + runTailSize.x + codePadding, position.y + ImGui::GetTextLineHeight()),
						context.Palette.CodeBlock, kCodeBackgroundRounding);

					for (std::size_t runIndex = wordIndex; runIndex <= lastWordOfRun; ++runIndex)
					{
						const ImVec2 runWordPosition = WordPosition(words[runIndex], origin, indent, lineHeight);
						const char* runWordBegin = words[runIndex].Text.c_str();
						const ImVec2 textPosition(runWordPosition.x + codePadding * 0.5f, runWordPosition.y);
						drawList->AddText(textPosition, ink, runWordBegin, TextEndWithoutTrailingSpaces(words[runIndex]));

						if (style == SpanStyle::BoldCode)
						{
							drawList->AddText(ImVec2(textPosition.x + kBoldOffset, textPosition.y), ink, runWordBegin,
											  TextEndWithoutTrailingSpaces(words[runIndex]));
						}
					}

					lastLineIndex = runTail.Line;
					wordIndex = lastWordOfRun;
					continue;
				}

				drawList->AddText(position, ink, wordBegin, wordEnd);

				if (isHeading || style == SpanStyle::Bold)
				{
					drawList->AddText(ImVec2(position.x + kBoldOffset, position.y), ink, wordBegin, wordEnd);
				}

				lastLineIndex = word.Line;
			}

			ImGui::PopFont();

			// WARNING: The words went through the draw list, so an item has to move the cursor past
			// them, or ImGui asserts on the next SetCursorPos.
			const int layoutLineCount = words.empty() ? 1 : lastLineIndex + 1;
			ImGui::Dummy(ImVec2(width, static_cast<float>(layoutLineCount) * lineHeight));
		}
	}

	void ConversationView::DrawRow(const Session& session, std::size_t index, const ConversationContext& context,
								   Core::EventBus& events)
	{
		const Row& row = m_Rows[index];
		const Turn& turn = session.GetTurns()[row.TurnIndex];

		const std::string id = std::format("{}/{}/{}/{}", static_cast<int>(row.Kind), row.TurnIndex, row.PartIndex,
										   row.ItemIndex);
		ImGui::PushID(id.c_str());

		switch (row.Kind)
		{
		case RowKind::User:
			DrawUser(turn.Prompt, turn.StartedAt, context);
			break;
		case RowKind::Gap:
			DrawGap(row.Pause, context);
			break;
		case RowKind::Part:
		{
			const TurnPart& part = m_Parts[row.TurnIndex][row.PartIndex];

			switch (part.Kind)
			{
			case PartKind::Reply:
				DrawReply(turn, part, index, context);
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
			case PartKind::Compaction:
				DrawCompaction(turn.Items[part.Items.front()], index, context);
				break;
			}
			break;
		}
		case RowKind::Step:
			ImGui::Indent();
			DrawStep(session, turn.Items[row.ItemIndex], index, context, events);
			ImGui::Unindent();
			break;
		case RowKind::RunUsage:
			DrawRunUsage(m_Parts[row.TurnIndex][row.PartIndex], index, context);
			break;
		}

		ImGui::PopID();
	}

	void ConversationView::Draw(const Session& session, const ConversationContext& context, Core::EventBus& events)
	{
		const ImVec2 fillAvailableSpace(0.0f, 0.0f);
		if (!ImGui::BeginChild("##conversation", fillAvailableSpace, ImGuiChildFlags_AlwaysUseWindowPadding,
							   ImGuiWindowFlags_None))
		{
			ImGui::EndChild();
			return;
		}

		const float width = ImGui::GetContentRegionAvail().x;

		if (session.GetVersion() != m_BuiltVersion || session.GetGeneration() != m_BuiltGeneration ||
			width != m_BuiltWidth)
		{
			Rebuild(session, width);
		}

		if (m_OffsetsDirty)
		{
			m_Offsets.assign(m_Rows.size() + 1, 0.0f);

			for (std::size_t rowIndex = 0; rowIndex < m_Rows.size(); ++rowIndex)
			{
				m_Offsets[rowIndex + 1] = m_Offsets[rowIndex] + m_Heights[rowIndex];
			}

			m_OffsetsDirty = false;
		}

		const float scrollTop = ImGui::GetScrollY();
		const float scrollBottom = scrollTop + ImGui::GetWindowHeight();
		const float scrollMax = ImGui::GetScrollMaxY();
		const bool wasAtEnd = scrollMax <= 0.0f || scrollTop >= scrollMax - kAtEndTolerance;

		const std::vector<float>::iterator firstOffsetBelowTop =
			std::upper_bound(m_Offsets.begin(), m_Offsets.end(), scrollTop);
		const std::size_t rowAtTop = firstOffsetBelowTop == m_Offsets.begin()
										 ? 0
										 : static_cast<std::size_t>(firstOffsetBelowTop - m_Offsets.begin()) - 1;
		const std::size_t firstRowToDraw = rowAtTop > 0 ? rowAtTop - 1 : 0;

		const float startY = ImGui::GetCursorPosY();

		for (std::size_t rowIndex = firstRowToDraw;
			 rowIndex < m_Rows.size() && m_Offsets[rowIndex] < scrollBottom + m_Heights[rowIndex]; ++rowIndex)
		{
			ImGui::SetCursorPosY(startY + m_Offsets[rowIndex]);
			const float cursorYBefore = ImGui::GetCursorPosY();

			DrawRow(session, rowIndex, context, events);

			const float heightTaken = ImGui::GetCursorPosY() - cursorYBefore;

			if (!m_Measured[rowIndex] || heightTaken != m_Heights[rowIndex])
			{
				m_Heights[rowIndex] = heightTaken;
				m_Measured[rowIndex] = true;
				m_OffsetsDirty = true;
			}
		}

		ImGui::SetCursorPosY(startY + m_Offsets.back());
		ImGui::Dummy(kNoSize);

		if (m_FollowTail && wasAtEnd)
		{
			const float bottomOfRow = 1.0f;
			ImGui::SetScrollHereY(bottomOfRow);
		}

		m_FollowTail = wasAtEnd;

		ImGui::EndChild();
	}
}
