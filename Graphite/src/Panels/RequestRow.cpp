#include "Core/Base/Base.h"

#include "Panels/RequestRow.h"

#include "Panels/SessionDrawing.h"
#include "Sessions/SessionText.h"
#include "Sessions/ToolFacts.h"

#include <IconsMaterialDesignIcons.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {
		// A label is cut to one line and to this much before it is fitted to the row: fitting
		// measures the text, and a tool result's summary can be long.
		constexpr std::size_t kLabelBytes = 400;

		struct LineOutcome
		{
			bool Clicked = false;
			bool Hovered = false;
		};

		// One item's line: its icon in its colour, its label cut to fit, and `column` pinned to
		// the right edge, which the label gives way to. A Selectable makes it one line tall
		// and clickable; the text is drawn after it, so its highlight lies under the text.
		LineOutcome ItemLine(const ItemLook& look, const std::string& label, const std::string& column,
							 const SessionPalette& palette, std::size_t id, bool selected, bool reveal)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;
			const float space = ImGui::GetStyle().ItemSpacing.x;
			const float height = ImGui::GetTextLineHeight();
			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

			if (reveal && !ImGui::IsRectVisible(ImVec2(std::max(right - start.x, 1.0f), height)))
				ImGui::SetScrollHereY(0.3f);

			ImGui::PushID(static_cast<int>(id));

			LineOutcome outcome;
			outcome.Clicked = ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, height));
			outcome.Hovered = ImGui::IsItemHovered();

			ImGui::PopID();

			ImVec2 at = start;
			DrawPiece(at, look.Colour, std::string{look.Icon} + " ");

			const float columnX = std::max(right - ImGui::CalcTextSize(column.c_str()).x, at.x);

			DrawPiece(at, ink, Fitted(SingleLineLabel(label, kLabelBytes), columnX - space - at.x));

			// Cut too: on a row narrower than the column itself it would run past the edge.
			at.x = columnX;
			DrawPiece(at, palette.TextDisabled, Fitted(column, right - at.x));

			return outcome;
		}

		// A section's own heading: the label left in TextDisabled, a right-aligned summary in
		// the same colour, the label giving way to it the way ItemLine's does. Not selectable,
		// but the row still ends with an item, per ImGui's rule.
		void SectionHeading(const std::string& heading, const std::string& summary, const SessionPalette& palette)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;
			const float space = ImGui::GetStyle().ItemSpacing.x;
			const float height = ImGui::GetTextLineHeight();

			ImVec2 at = start;
			const float columnX = std::max(right - ImGui::CalcTextSize(summary.c_str()).x, at.x);

			DrawPiece(at, palette.TextDisabled, Fitted(heading, columnX - space - at.x));

			at.x = columnX;
			DrawPiece(at, palette.TextDisabled, Fitted(summary, right - at.x));

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
		case SentKind::QueuedPrompt: return {ICON_MDI_ACCOUNT_OUTLINE, palette.Accent};
		case SentKind::Injected:
		case SentKind::Reminder: return {ICON_MDI_BELL_OUTLINE, palette.Injected};
		case SentKind::ToolsLoaded: return {ICON_MDI_TOOLS, palette.Agent};
		case SentKind::Skills: return {ICON_MDI_PUZZLE_OUTLINE, palette.Agent};
		case SentKind::McpInstructions: return {ICON_MDI_SERVER_NETWORK, palette.Agent};
		case SentKind::EditedFile: return {ICON_MDI_PENCIL, palette.Edit};
		case SentKind::Image: return {ICON_MDI_IMAGE_OUTLINE, palette.Accent};
		case SentKind::SystemPrompt: return {ICON_MDI_TEXT_BOX_OUTLINE, palette.Injected};
		case SentKind::ToolSchemas: return {ICON_MDI_CODE_BRACES, palette.Agent};
		}

		return {ICON_MDI_HELP_CIRCLE_OUTLINE, palette.Other};
	}

	ItemLook LookOf(const ResponseItem& item, const SessionPalette& palette)
	{
		switch (item.Kind)
		{
		case ResponseKind::Thinking: return {ICON_MDI_BRAIN, palette.TextDisabled};
		case ResponseKind::Text: return {ICON_MDI_MESSAGE_TEXT_OUTLINE, palette.Accent};
		case ResponseKind::ToolCall:
		{
			const ToolCategory category = CategoryOf(item.ToolName);
			return {IconOf(category), palette.CategoryColor(category)};
		}
		case ResponseKind::Other: break;
		}

		return {ICON_MDI_HELP_CIRCLE_OUTLINE, palette.TextDisabled};
	}

	std::string SentColumn(const SentItem& item)
	{
		const std::string column = FormatBytes(item.Bytes);

		// U+2248 ALMOST EQUAL TO.
		return item.Approximate ? "\xe2\x89\x88 " + column : column;
	}

	std::string ShortModelName(std::string_view model)
	{
		return std::string{model.starts_with("claude-") ? model.substr(7) : model};
	}

	std::string SignedTokens(std::int64_t tokens)
	{
		const std::uint64_t magnitude =
			tokens < 0 ? static_cast<std::uint64_t>(-tokens) : static_cast<std::uint64_t>(tokens);

		// U+2212 MINUS SIGN: as wide as the plus, so a column of deltas lines up.
		return (tokens < 0 ? "\xe2\x88\x92" : "+") + FormatTokens(magnitude);
	}

	// A wait for the model is seconds, mostly: the tenth tells a quick request from a slow one
	// where FormatDuration's whole seconds would not.
	std::string WaitText(std::chrono::milliseconds waited)
	{
		if (waited < std::chrono::seconds{1})
			return std::to_string(waited.count()) + " ms";

		if (waited < std::chrono::minutes{1})
			return std::format("{:.1f} s", static_cast<double>(waited.count()) / 1000.0);

		return FormatDuration(waited);
	}

	const char* StopGlyph(std::string_view reason)
	{
		if (reason == "end_turn")
			return ICON_MDI_STOP_CIRCLE_OUTLINE;
		if (reason == "tool_use")
			return ICON_MDI_WRENCH_OUTLINE;
		return ICON_MDI_HELP_CIRCLE_OUTLINE;
	}

	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock, ClockStyle time)
	{
		std::string line = "#" + std::to_string(request.Index);

		if (const std::string written = clock.Format(request.RespondedAt, time); !written.empty())
			line += " " + written;

		if (!request.Model.empty())
			line += kDot + ShortModelName(request.Model) + (request.Fast ? " fast" : "");

		if (request.Usage.Total() > 0)
		{
			line += kDot + std::string{"ctx "} + FormatTokens(request.Context);

			if (request.ContextDelta)
				line += " " + SignedTokens(*request.ContextDelta);

			if (request.Context > 0)
				line += kDot + std::to_string(CachedPercent(request.Usage.CacheRead, request.Context)) + "% cache";

			line += kDot + FormatTokens(request.Usage.Output) + " out";
			line += kDot + FormatCost(pricing.CostOf({{UsageKey{request.Model, request.Fast}, request.Usage}}));
		}

		if (request.Waited)
			line += kDot + WaitText(*request.Waited);

		return line;
	}

	void DrawRequestLine(ImVec2 at, float right, const ApiRequest& request, const Pricing& pricing,
						 const SessionPalette& palette, LocalClock& clock)
	{
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

		// Each piece takes what room the ones before it left, so a narrow row keeps the request's
		// numbers and loses its tail first.
		DrawPiece(at, ink, Fitted(RequestLine(request, pricing, clock), right - at.x));

		if (request.CacheRebuilt)
			DrawPiece(at, palette.Edit, Fitted(std::string{" "} + ICON_MDI_CACHED, right - at.x));

		const std::string stop = kDot + std::string{StopGlyph(request.StopReason)} +
								 (request.StopReason.empty() ? "" : " " + SingleLineLabel(request.StopReason, 60));
		DrawPiece(at, palette.TextDisabled, Fitted(stop, right - at.x));

		std::string tools;
		for (const std::string& name : ToolNamesUsed(request))
			tools += (tools.empty() ? kDot : ", ") + SingleLineLabel(name, 80);

		DrawPiece(at, palette.TextDisabled, Fitted(tools, right - at.x));
	}

	DetailsPick DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette,
								   std::optional<std::size_t> selected, std::optional<std::size_t> reveal)
	{
		DetailsPick pick;

		const auto line = [&](const ItemLook& look, const std::string& label, const std::string& column, std::size_t id)
		{
			const LineOutcome outcome = ItemLine(look, label, column, palette, id, selected == id, reveal == id);

			if (outcome.Clicked)
				pick.Clicked = id;
			if (outcome.Hovered)
				pick.Hovered = id;
		};

		SectionHeading(SentHeading(request), SentSummary(request), palette);

		ImGui::Indent();
		for (std::size_t i = 0; i < request.Sent.size(); ++i)
			line(LookOf(request.Sent[i], palette), request.Sent[i].Label, SentColumn(request.Sent[i]), i);
		ImGui::Unindent();

		SectionHeading(ReplyHeading(request), ReplySummary(request), palette);

		ImGui::Indent();
		for (std::size_t i = 0; i < request.Response.size(); ++i)
			line(LookOf(request.Response[i], palette), request.Response[i].Label, {}, request.Sent.size() + i);
		ImGui::Unindent();

		ColouredText(palette.TextDisabled, ToolsAvailableLabel(request));

		return pick;
	}
}
