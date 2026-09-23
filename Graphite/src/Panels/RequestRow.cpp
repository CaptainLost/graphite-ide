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

		std::string ShortModel(std::string_view model)
		{
			return std::string{model.starts_with("claude-") ? model.substr(7) : model};
		}

		// A wait for the model is seconds, mostly: the tenth tells a quick request from a slow
		// one where FormatDuration's whole seconds would not.
		std::string WaitText(std::chrono::milliseconds waited)
		{
			if (waited < std::chrono::seconds{1})
				return std::to_string(waited.count()) + " ms";

			if (waited < std::chrono::minutes{1})
				return std::format("{:.1f} s", static_cast<double>(waited.count()) / 1000.0);

			return FormatDuration(waited);
		}

		std::string SignedTokens(std::int64_t tokens)
		{
			const std::uint64_t magnitude =
				tokens < 0 ? static_cast<std::uint64_t>(-tokens) : static_cast<std::uint64_t>(tokens);
			return (tokens < 0 ? "-" : "+") + FormatTokens(magnitude);
		}

		const char* StopGlyph(std::string_view reason)
		{
			if (reason == "end_turn")
				return ICON_MDI_STOP_CIRCLE_OUTLINE;
			if (reason == "tool_use")
				return ICON_MDI_WRENCH_OUTLINE;
			return ICON_MDI_HELP_CIRCLE_OUTLINE;
		}

		struct Look
		{
			const char* Icon = ICON_MDI_HELP_CIRCLE_OUTLINE;
			ImU32 Colour = 0;
		};

		Look LookOf(const SentItem& item, const SessionPalette& palette)
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
			}

			return {ICON_MDI_HELP_CIRCLE_OUTLINE, palette.Other};
		}

		Look LookOf(const ResponseItem& item, const SessionPalette& palette)
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

		// One item's line: its icon in its colour, its label cut to fit, and `column` pinned to
		// the right edge, which the label gives way to. A Dummy makes it one line tall.
		void ItemLine(const Look& look, const std::string& label, const std::string& column,
					  const SessionPalette& palette)
		{
			const ImVec2 start = ImGui::GetCursorScreenPos();
			const float right = start.x + ImGui::GetContentRegionAvail().x;
			const float space = ImGui::GetStyle().ItemSpacing.x;
			const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);

			ImVec2 at = start;
			DrawPiece(at, look.Colour, std::string{look.Icon} + " ");

			const float columnX = std::max(right - ImGui::CalcTextSize(column.c_str()).x, at.x);

			DrawPiece(at, ink, Fitted(SingleLineLabel(label, kLabelBytes), columnX - space - at.x));

			at.x = columnX;
			DrawPiece(at, palette.TextDisabled, column);

			ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
		}
	}

	std::string RequestLine(const ApiRequest& request, const Pricing& pricing, LocalClock& clock)
	{
		std::string line = "#" + std::to_string(request.Index);

		if (const std::string time = clock.HoursMinutesSeconds(request.RespondedAt); !time.empty())
			line += " " + time;

		if (!request.Model.empty())
			line += kDot + ShortModel(request.Model) + (request.Fast ? " fast" : "");

		if (request.Usage.Total() > 0)
		{
			line += kDot + std::string{"ctx "} + FormatTokens(request.Context);

			if (request.ContextDelta)
				line += " " + SignedTokens(*request.ContextDelta);

			if (request.Context > 0)
				line += kDot + std::format("{:.0f}% cache", request.CachedShare * 100.0);

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

	void DrawRequestDetails(const ApiRequest& request, const SessionPalette& palette)
	{
		ColouredText(palette.TextDisabled, "Sent");

		if (request.Sent.empty())
			ColouredText(palette.TextDisabled, "nothing new");

		for (const SentItem& item : request.Sent)
		{
			std::string column = FormatBytes(item.Bytes);
			if (item.EstimatedTokens > 0)
				column += "  ~" + FormatTokens(static_cast<std::uint64_t>(item.EstimatedTokens)) + " tok";

			ItemLine(LookOf(item, palette), item.Label, column, palette);
		}

		ColouredText(palette.TextDisabled, "Response");

		if (request.Response.empty())
			ColouredText(palette.TextDisabled, "nothing yet");

		for (const ResponseItem& item : request.Response)
			ItemLine(LookOf(item, palette), item.Label, {}, palette);

		ColouredText(palette.TextDisabled, "Tools available: " + std::to_string(request.ToolsAvailable));
	}
}
