#include "Sessions/SessionList.h"

#include "Sessions/LocalClock.h"
#include "Sessions/Overview/DashboardTips.h"
#include "Sessions/Overview/TipCardView.h"
#include "Sessions/Overview/TipText.h"
#include "Sessions/Pricing.h"
#include "Sessions/SessionText.h"
#include "UI/FittedText.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"

#include <imgui.h>

#include <algorithm>
#include <format>

namespace Graphite {

	namespace {
		constexpr std::size_t kTitleBytes = 200;
		constexpr float kWorktreeGapInFontSizes = 0.5f;

		// WARNING: Liveness comes from the fingerprint, which the index refreshes whenever the transcript changes.
		// Asking the filesystem instead would cost a stat per row per frame.
		bool IsLive(const SessionSummary& summary)
		{
			if (summary.Print.WriteTicks == 0)
			{
				return false;
			}

			const std::filesystem::file_time_type written{
				std::filesystem::file_time_type::duration{summary.Print.WriteTicks}};
			return std::filesystem::file_time_type::clock::now() - written < kLiveWindow;
		}

		std::string DurationOf(const SessionSummary& summary)
		{
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> start = ParseTimestamp(summary.StartedAt);
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> end = ParseTimestamp(summary.EndedAt);
			return start && end ? FormatDuration(*end - *start) : std::string{};
		}

		bool HasHourOn(const std::map<UtcHour, HourActivity>& hours, std::chrono::year_month_day day,
					   const LocalCalendar& calendar)
		{
			return std::ranges::any_of(hours, [&](const auto& entry) { return calendar.At(entry.first).Day == day; });
		}

		// WARNING: A session in the model answers from its days, already put on the calendar when the model was built,
		// since asking the calendar for every hour of every session would cost a time zone lookup each, every frame.
		bool IsActiveOn(const ListedSession& session, const DashboardModel& model, std::chrono::year_month_day day,
						const LocalCalendar& calendar)
		{
			if (session.ModelIndex.has_value() && *session.ModelIndex < model.Sessions.size())
			{
				return model.Sessions[*session.ModelIndex].CostByDay.contains(day);
			}

			return ActiveOnDay(*session.Summary, day, calendar);
		}

		bool RowPrecedes(const SessionListRow& earlier, const SessionListRow& later)
		{
			if (earlier.Group != later.Group)
			{
				return earlier.Group < later.Group;
			}

			return earlier.Session->Summary->EndedAt > later.Session->Summary->EndedAt;
		}

		bool IsEmpty(const TipCard& card)
		{
			return card.Head.empty() && card.Body.empty() && card.Notes.empty() && card.Sections.empty();
		}

		CostResult TotalCost(const SessionSummary& summary, const Pricing& pricing)
		{
			CostResult total = pricing.CostOf(summary.Main);
			total += pricing.CostOf(summary.Subagents);
			return total;
		}

		TipCard RowTip(const ListedSession& session, const CostResult& total, const SessionListContext& context)
		{
			TipCard card = session.ModelIndex.has_value() ? SessionTip(context.Model, *session.ModelIndex, context.Calendar)
														  : TipCard{};

			if (std::string hint = UnknownCostHint(total); !hint.empty())
			{
				if (card.Head.empty())
				{
					card.Head = PlaceLabel(session.Place);
				}

				card.Notes.push_back(std::move(hint));
			}

			return card;
		}

		SessionRowText RowText(const SessionSummary& summary, const SessionPlace& place, const CostResult& total,
							   const SessionListContext& context)
		{
			SessionRowText text{.Title = (IsLive(summary) ? std::format("{} ", kLiveSessionMark) : std::string{}) +
										 SingleLineLabel(summary.DisplayTitle(), kTitleBytes),
								.Worktree = place.Worktree,
								.Detail = FormatCost(total)};

			if (summary.SubagentCount > 0)
			{
				text.Detail += " (subagents " + FormatCost(context.Prices.CostOf(summary.Subagents)) + ")";
			}
			if (const std::string duration = DurationOf(summary); !duration.empty())
			{
				text.Detail += "  " + duration;
			}
			if (const std::string ended = context.Clock.Format(summary.EndedAt, ClockStyle::Date); !ended.empty())
			{
				text.Detail += "  " + ended;
			}

			return text;
		}

		SessionRowAction DrawRow(const ListedSession& session, const SessionListContext& context)
		{
			const SessionSummary& summary = *session.Summary;
			ImGui::PushID(Core::ToUtf8(summary.File).c_str());

			const CostResult total = TotalCost(summary, context.Prices);
			const SessionRowResult row = DrawSessionRow(RowText(summary, session.Place, total, context));

			if (row.Hovered)
			{
				const TipCard card = RowTip(session, total, context);

				if (!IsEmpty(card) && BeginTipCard(context.Colours))
				{
					DrawTipCard(card, context.Colours);
					ImGui::EndTooltip();
				}
			}

			ImGui::PopID();
			return row.Action;
		}

		bool DrawGroupHeader(std::span<const SessionListRow> rows, std::size_t first, const SessionListContext& context)
		{
			const std::filesystem::path::string_type& group = rows[first].Group;
			CostResult total;

			for (std::size_t member = first; member < rows.size() && rows[member].Group == group; ++member)
			{
				total += TotalCost(*rows[member].Session->Summary, context.Prices);
			}

			// WARNING: The id must never be empty. A transcript with no cwd leaves its place empty, and an empty id
			// hashes to the child window's own id and asserts. The id is the group's key, not the text shown, which
			// changes as the group's sessions do.
			const std::string repository = Core::ToUtf8(rows[first].Session->Place.Repository);
			const std::string label = repository.empty() ? "(unknown folder)" : repository;
			const std::string cost = FormatCost(total);
			const std::string groupId = "group-" + Core::ToUtf8(std::filesystem::path{group});
			const bool open = ChevronTreeNode(groupId.c_str(), ImGuiTreeNodeFlags_CollapsingHeader | ImGuiTreeNodeFlags_DefaultOpen,
											  label.c_str(), cost.c_str());

			if (const std::string hint = UnknownCostHint(total); !hint.empty() && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", hint.c_str());
			}

			return open;
		}
	}

	bool ActiveOnDay(const SessionSummary& summary, std::chrono::year_month_day day, const LocalCalendar& calendar)
	{
		return HasHourOn(summary.Activity.MainHours, day, calendar) ||
			   HasHourOn(summary.Activity.SubagentHours, day, calendar);
	}

	std::string DayChipText(std::chrono::year_month_day day, std::size_t sessions)
	{
		return WeekdayDate(day) + std::string{kMiddleDotSeparator} + std::to_string(sessions);
	}

	// WARNING: The title is drawn over an id-only row, not as a Selectable's label, which a "##" in a session's title
	// would cut short. It stands the row end's inset in from the list's edge, inside the row's inset highlight, and
	// the dim line under it keeps to the title's indent.
	SessionRowResult DrawSessionRow(const SessionRowText& text)
	{
		constexpr bool kNeverSelected = false;
		SessionRowResult result;

		const ImVec2 rowStart = ImGui::GetCursorPos();

		if (ListRowSelectable("##session", kNeverSelected))
		{
			result.Action = SessionRowAction::Open;
		}

		result.Hovered = ImGui::IsItemHovered();

		{
			const ListPopupScope popup;

			if (Core::BeginRightClickPopup("##sessionMenu"))
			{
				if (ImGui::MenuItem("Continue in terminal"))
				{
					result.Action = SessionRowAction::Continue;
				}

				ImGui::EndPopup();
			}
		}

		ImGui::Indent(ListRowEndInset());
		ImGui::SetCursorPos(ImVec2(rowStart.x + ListRowEndInset(), ListRowTextTop(rowStart.y)));

		const float gap = ImGui::GetFontSize() * kWorktreeGapInFontSizes;
		const float worktreeRoom = text.Worktree.empty() ? 0.0f : ImGui::CalcTextSize(text.Worktree.c_str()).x + gap;
		const float titleRoom = std::max(0.0f, ListRowEndPosX() - ImGui::GetCursorPosX() - worktreeRoom);
		ImGui::TextUnformatted(FitToWidth(text.Title, titleRoom).c_str());

		if (!text.Worktree.empty())
		{
			ImGui::SameLine(0.0f, gap);
			ImGui::TextDisabled("%s", FitToWidth(text.Worktree, ListRowEndPosX() - ImGui::GetCursorPosX()).c_str());
		}

		ImGui::Indent();
		ImGui::TextDisabled("%s", text.Detail.c_str());
		ImGui::Unindent();
		ImGui::Unindent(ListRowEndInset());

		return result;
	}

	std::vector<SessionListRow> SessionListRows(std::span<const ListedSession> sessions, const DashboardModel& model,
												const SessionListFilter& filter, const IsSessionOpen& isOpen,
												const LocalCalendar& calendar)
	{
		const std::string title = Core::Lowered(filter.Title);
		std::vector<SessionListRow> rows;

		for (const ListedSession& session : sessions)
		{
			const SessionSummary& summary = *session.Summary;

			if (!ShowsInSessionList(summary, IsLive(summary) || isOpen(summary)))
			{
				continue;
			}

			if (filter.Day.has_value() && !IsActiveOn(session, model, *filter.Day, calendar))
			{
				continue;
			}

			if (!title.empty() && !Core::LoweredTextContains(summary.DisplayTitle(), title))
			{
				continue;
			}

			rows.push_back({.Session = &session,
							.Group = filter.Grouped ? DirectoryKey(session.Place.Repository)
													: std::filesystem::path::string_type{}});
		}

		std::ranges::sort(rows, RowPrecedes);
		return rows;
	}

	std::size_t SessionsOnDay(std::span<const ListedSession> sessions, const DashboardModel& model,
							  std::chrono::year_month_day day, const LocalCalendar& calendar)
	{
		return static_cast<std::size_t>(std::ranges::count_if(
			sessions, [&](const ListedSession& session) { return IsActiveOn(session, model, day, calendar); }));
	}

	SessionListAction DrawSessionList(std::span<const SessionListRow> rows, bool grouped, const SessionListContext& context)
	{
		SessionListAction action;

		if (ImGui::BeginChild("##sessionList"))
		{
			const ListBodyScope listBody;
			bool groupOpen = true;

			for (std::size_t index = 0; index < rows.size(); ++index)
			{
				if (grouped && (index == 0 || rows[index].Group != rows[index - 1].Group))
				{
					groupOpen = DrawGroupHeader(rows, index, context);
				}

				if (grouped && !groupOpen)
				{
					continue;
				}

				if (const SessionRowAction row = DrawRow(*rows[index].Session, context); row != SessionRowAction::None)
				{
					action = {.Action = row, .Summary = rows[index].Session->Summary};
				}
			}
		}

		ImGui::EndChild();
		return action;
	}
}
