#include "Sessions/Requests/RequestRow.h"

#include "Core/Base/Base.h"

#include "Sessions/SessionDrawing.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/ByteSize.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {
		constexpr std::string_view kAlmostEqualSign = "\xe2\x89\x88 ";

		constexpr float kMinimumVisibleWidth = 1.0f;
		constexpr float kRevealScrollRatio = 0.3f;

		constexpr double kMillisecondsPerSecond = 1000.0;

		constexpr std::size_t kLongestStopReasonBytes = 60;
		constexpr std::size_t kLongestToolNameBytes = 80;

		struct LineOutcome
		{
			bool Clicked = false;
			bool Hovered = false;
		};

		LineOutcome ItemLine(const ItemLook& look, const std::string& label, const std::string& column,
							 const SessionPalette& palette, std::size_t id, bool selected, bool reveal)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;
			const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
			const float height = ImGui::GetTextLineHeight();
			const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

			if (reveal && !ImGui::IsRectVisible(ImVec2(std::max(right - start.x, kMinimumVisibleWidth), height)))
			{
				ImGui::SetScrollHereY(kRevealScrollRatio);
			}

			ImGui::PushID(static_cast<int>(id));

			// WARNING: The text is drawn after the Selectable so its highlight lies under the text.
			LineOutcome outcome;
			outcome.Clicked = ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
			outcome.Hovered = ImGui::IsItemHovered();

			ImGui::PopID();

			ImVec2 drawPosition = start;
			DrawPiece(drawPosition, look.Colour, std::string{look.Icon} + " ");

			const float columnX = std::max(right - ImGui::CalcTextSize(column.c_str()).x, drawPosition.x);

			DrawPiece(drawPosition, textColour,
					  Fitted(SingleLineLabel(label, kLongestLabelBytes), columnX - itemSpacing - drawPosition.x));

			// WARNING: The column is fitted too: on a row narrower than the column itself it would run
			// past the edge.
			drawPosition.x = columnX;
			DrawPiece(drawPosition, palette.TextDisabled, Fitted(column, right - drawPosition.x));

			return outcome;
		}

		void DrawPickableLine(DetailsPick& pick, const ItemLook& look, const std::string& label,
							  const std::string& column, const SessionPalette& palette, std::size_t id,
							  std::optional<std::size_t> selected, std::optional<std::size_t> reveal)
		{
			const bool isSelected = selected == id;
			const bool isRevealed = reveal == id;
			const LineOutcome outcome = ItemLine(look, label, column, palette, id, isSelected, isRevealed);

			if (outcome.Clicked)
			{
				pick.Clicked = id;
			}
			if (outcome.Hovered)
			{
				pick.Hovered = id;
			}
		}

		void SectionHeading(const std::string& heading, const std::string& summary, const SessionPalette& palette)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;
			const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
			const float height = ImGui::GetTextLineHeight();

			ImVec2 drawPosition = start;
			const float columnX = std::max(right - ImGui::CalcTextSize(summary.c_str()).x, drawPosition.x);

			DrawPiece(drawPosition, palette.TextDisabled, Fitted(heading, columnX - itemSpacing - drawPosition.x));

			drawPosition.x = columnX;
			DrawPiece(drawPosition, palette.TextDisabled, Fitted(summary, right - drawPosition.x));

			ImGui::Dummy(ImVec2(0.0f, height));
		}
	}

	ItemLook LookOf(const SentItem& item, const SessionPalette& palette)
	{
		switch (item.Kind)
		{
		case SentKind::ToolResult:
		{
			const ToolCategory category = CategoryOf(item.ToolName);
			return {IconOf(category), item.IsError ? palette.Failed : palette.CategoryColor(category)};
		}
		case SentKind::Prompt:
		case SentKind::QueuedPrompt:
			return {ICON_MDI_ACCOUNT_OUTLINE, palette.Accent};
		case SentKind::Injected:
		case SentKind::Reminder:
			return {ICON_MDI_BELL_OUTLINE, palette.Injected};
		case SentKind::ToolsLoaded:
			return {ICON_MDI_TOOLS, palette.Agent};
		case SentKind::Skills:
			return {ICON_MDI_PUZZLE_OUTLINE, palette.Agent};
		case SentKind::McpInstructions:
			return {ICON_MDI_SERVER_NETWORK, palette.Agent};
		case SentKind::EditedFile:
			return {ICON_MDI_PENCIL, palette.Edit};
		case SentKind::Image:
			return {ICON_MDI_IMAGE_OUTLINE, palette.Accent};
		case SentKind::SystemPrompt:
			return {ICON_MDI_TEXT_BOX_OUTLINE, palette.Injected};
		case SentKind::ToolSchemas:
			return {ICON_MDI_CODE_BRACES, palette.Agent};
		}

		return {ICON_MDI_HELP_CIRCLE_OUTLINE, palette.Other};
	}

	ItemLook LookOf(const ResponseItem& item, const SessionPalette& palette)
	{
		switch (item.Kind)
		{
		case ResponseKind::Thinking:
			return {ICON_MDI_BRAIN, palette.TextDisabled};
		case ResponseKind::Text:
			return {ICON_MDI_MESSAGE_TEXT_OUTLINE, palette.Accent};
		case ResponseKind::ToolCall:
		{
			const ToolCategory category = CategoryOf(item.ToolName);
			return {IconOf(category), palette.CategoryColor(category)};
		}
		case ResponseKind::Other:
			break;
		}

		return {ICON_MDI_HELP_CIRCLE_OUTLINE, palette.TextDisabled};
	}

	// WARNING: No token figure is shown: the transcript records tokens per request, not per item, so
	// a per-item count would only ever be a guess.
	std::string SentColumn(const SentItem& item)
	{
		const std::string column = Core::FormatByteSize(item.Bytes);

		return item.Approximate ? std::string{kAlmostEqualSign} + column : column;
	}

	std::string SignedTokens(std::int64_t tokens)
	{
		const std::uint64_t magnitude =
			tokens < 0 ? static_cast<std::uint64_t>(-tokens) : static_cast<std::uint64_t>(tokens);

		return std::format("{}{}", tokens < 0 ? kMinusSign : std::string_view{"+"}, FormatTokens(magnitude));
	}

	// WARNING: The tenth of a second tells a quick request from a slow one where FormatDuration's
	// whole seconds would not.
	std::string WaitText(std::chrono::milliseconds waited)
	{
		if (waited < std::chrono::seconds{1})
		{
			return std::to_string(waited.count()) + " ms";
		}

		if (waited < std::chrono::minutes{1})
		{
			return std::format("{:.1f} s", static_cast<double>(waited.count()) / kMillisecondsPerSecond);
		}

		return FormatDuration(waited);
	}

	const char* StopGlyph(std::string_view reason)
	{
		if (reason == "end_turn")
		{
			return ICON_MDI_STOP_CIRCLE_OUTLINE;
		}
		if (reason == "tool_use")
		{
			return ICON_MDI_WRENCH_OUTLINE;
		}
		return ICON_MDI_HELP_CIRCLE_OUTLINE;
	}

	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock,
							ClockStyle clockStyle)
	{
		std::string line = "#" + std::to_string(request.Index);

		if (const std::string written = clock.Format(request.RespondedAt, clockStyle); !written.empty())
		{
			line += " " + written;
		}

		if (!request.Model.empty())
		{
			line += kMiddleDotSeparator;
			line += ModelLabel(request.Model, request.Fast);
		}

		if (request.Usage.Total() > 0)
		{
			line += kMiddleDotSeparator;
			line += "ctx ";
			line += FormatTokens(request.Context);

			if (request.ContextDelta)
			{
				line += " " + SignedTokens(*request.ContextDelta);
			}

			if (request.Context > 0)
			{
				line += kMiddleDotSeparator;
				line += std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "% cache";
			}

			line += kMiddleDotSeparator;
			line += FormatTokens(request.Usage.Output) + " out";
			line += kMiddleDotSeparator;
			line += FormatCost(pricing.CostOf(request));
		}

		if (request.Waited)
		{
			line += kMiddleDotSeparator;
			line += WaitText(*request.Waited);
		}

		return line;
	}

	// WARNING: Draws no item. The caller puts the Selectable or Dummy under it that makes the row
	// one line tall.
	void DrawRequestLine(ImVec2 drawPosition, float right, const ApiRequest& request, const Pricing& pricing,
						 const SessionPalette& palette, LocalClock& clock)
	{
		const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);

		DrawPiece(drawPosition, textColour, Fitted(RequestLine(request, pricing, clock), right - drawPosition.x));

		if (request.CacheRebuilt)
		{
			DrawPiece(drawPosition, palette.Edit, Fitted(std::string{" "} + ICON_MDI_CACHED, right - drawPosition.x));
		}

		std::string stop{kMiddleDotSeparator};
		stop += StopGlyph(request.StopReason);
		if (!request.StopReason.empty())
		{
			stop += " " + SingleLineLabel(request.StopReason, kLongestStopReasonBytes);
		}
		DrawPiece(drawPosition, palette.TextDisabled, Fitted(stop, right - drawPosition.x));

		std::string tools;
		for (const std::string& toolName : ToolNamesUsed(request))
		{
			if (tools.empty())
			{
				tools += kMiddleDotSeparator;
			}
			else
			{
				tools += ", ";
			}
			tools += SingleLineLabel(toolName, kLongestToolNameBytes);
		}

		DrawPiece(drawPosition, palette.TextDisabled, Fitted(tools, right - drawPosition.x));
	}

	DetailsPick DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette,
								   std::optional<std::size_t> selected, std::optional<std::size_t> reveal)
	{
		DetailsPick pick;
		const std::string noColumn;

		SectionHeading(SentHeading(request), SentSummary(request), palette);

		ImGui::Indent();
		for (std::size_t sentIndex = 0; sentIndex < request.Sent.size(); ++sentIndex)
		{
			DrawPickableLine(pick, LookOf(request.Sent[sentIndex], palette), request.Sent[sentIndex].Label,
							 SentColumn(request.Sent[sentIndex]), palette, sentIndex, selected, reveal);
		}
		ImGui::Unindent();

		SectionHeading(ReplyHeading(request), ReplySummary(request), palette);

		ImGui::Indent();
		for (std::size_t responseIndex = 0; responseIndex < request.Response.size(); ++responseIndex)
		{
			DrawPickableLine(pick, LookOf(request.Response[responseIndex], palette),
							 request.Response[responseIndex].Label, noColumn, palette, request.Sent.size() + responseIndex,
							 selected, reveal);
		}
		ImGui::Unindent();

		ColouredText(palette.TextDisabled, ToolsAvailableLabel(request));

		return pick;
	}
}
