#include "Sessions/Overview/DashboardTips.h"

#include "Sessions/Overview/TipText.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::size_t kTopProjects = 3;
		constexpr std::size_t kTopSubagentSessions = 4;
		constexpr std::size_t kListedSessions = 5;
		constexpr std::size_t kDaysPerWeek = 7;
		constexpr std::size_t kHoursPerDay = 24;

		enum class Requesters : std::uint8_t
		{
			Everyone,
			Subagents
		};

		enum class Amount : std::uint8_t
		{
			Money,
			Tokens
		};

		template <typename Pick>
		FamilyColumn ColumnOf(const DashboardModel& model, Pick pick, Requesters requesters, Amount amount)
		{
			FamilyColumn column;

			for (std::size_t index = 0; index < kModelFamilyCount; ++index)
			{
				const FamilyTotals& family = model.Families[index];
				const bool subagents = requesters == Requesters::Subagents;
				column.Amounts[index] = static_cast<double>(pick(family));
				column.Requests[index] = subagents ? family.SubagentRequests : family.Requests;

				if (amount == Amount::Money)
				{
					column.Unpriced[index] = subagents ? family.UnpricedSubagentRequests : family.UnpricedRequests;
				}
			}

			return column;
		}

		std::set<std::string> UnpricedModelsOf(const DashboardModel& model, const FamilyColumn& column)
		{
			std::set<std::string> models;

			for (std::size_t index = 0; index < kModelFamilyCount; ++index)
			{
				if (column.Unpriced[index] > 0)
				{
					models.insert(model.Families[index].UnpricedModels.begin(), model.Families[index].UnpricedModels.end());
				}
			}

			return models;
		}

		TipSection ByModel(const DashboardModel& model, std::string title, const FamilyColumn& column, double total,
						   std::string (*format)(double))
		{
			TipSection section{.Title = std::move(title)};
			AddFamilyRows(section.Rows, model, column, total, format);
			NoteUnpriced(section.Notes, UnpricedModelsOf(model, column));
			return section;
		}

		void AddThreadRows(std::vector<TipRow>& rows, double cost, double subagentCost)
		{
			rows.push_back({"Main thread", TipMoney(cost - subagentCost), TipShare(cost - subagentCost, cost)});
			rows.push_back({"Subagents", TipMoney(subagentCost), TipShare(subagentCost, cost)});
		}

		std::string DashIfNone(std::size_t divisor, std::string text)
		{
			return divisor == 0 ? std::string{kEnDash} : std::move(text);
		}

		double Per(double amount, std::size_t divisor)
		{
			return divisor == 0 ? 0.0 : amount / static_cast<double>(divisor);
		}

		std::string Ratio(double cost, double average)
		{
			const double ratio = cost / average;

			if (ratio >= 1.0)
			{
				return std::format("{:.1f}{} an average active day ({})", ratio, kMultiplicationSign, TipMoney(average));
			}

			return std::format("{}% of an average active day ({})", std::lround(100.0 * ratio), TipMoney(average));
		}

		std::chrono::minutes ActiveTimeOf(const SessionFacts& session)
		{
			return session.Summary == nullptr ? std::chrono::minutes{0} : session.Summary->Activity.ActiveTime;
		}

		TipCard SpentTip(const DashboardModel& model)
		{
			TipCard card{.Head = std::format("Spent in {} days", model.Days.size()), .HeadValue = TipMoney(model.Cost)};

			card.Sections.push_back(ByModel(model, "By model",
											ColumnOf(model, [](const FamilyTotals& family) { return family.Cost; },
													 Requesters::Everyone, Amount::Money),
											model.Cost, TipMoney));

			TipSection byThread{.Title = "By thread"};
			AddThreadRows(byThread.Rows, model.Cost, model.SubagentCost);
			card.Sections.push_back(std::move(byThread));

			TipSection projects{.Title = "Top projects"};
			for (std::size_t index = 0; index < std::min(kTopProjects, model.Projects.size()); ++index)
			{
				const ProjectTotals& project = model.Projects[index];
				projects.Rows.push_back(
					{ProjectName(project.Repository), TipMoney(project.Cost), TipShare(project.Cost, model.Cost)});
			}
			card.Sections.push_back(std::move(projects));

			if (model.UnpricedRequests == 1)
			{
				CloseTip(card, "1 request went to a model with no price and counts as $0.");
			}
			else if (model.UnpricedRequests > 1)
			{
				CloseTip(card, std::format("{} requests went to a model with no price and count as $0.",
										   TipCount(model.UnpricedRequests)));
			}

			return card;
		}

		TipCard ThisWeekTip(const DashboardModel& model)
		{
			TipCard card{.Head = "This week, day by day"};
			TipSection days;

			for (const WeekDay& day : ThisWeek(model))
			{
				const std::chrono::weekday weekday{std::chrono::sys_days{day.Day}};
				days.Rows.push_back({std::format("{} {}", WeekdayAbbreviation(static_cast<int>(weekday.iso_encoding()) - 1),
												 static_cast<unsigned>(day.Day.day())),
									 TipMoney(day.Cost), TipMoney(day.WeekBefore) + " a week before"});
			}

			days.Notes.push_back("Compared with the same days of last week, so a Tuesday is not set against a full week.");
			card.Sections.push_back(std::move(days));
			return card;
		}

		TipCard PerActiveDayTip(const DashboardModel& model)
		{
			const ActiveDayStats stats = ActiveDays(model);
			TipCard card{.Head = "Cost of an active day"};

			TipSection costs;
			costs.Rows.push_back({"Average", TipMoney(stats.Average)});
			costs.Rows.push_back({"Median", TipMoney(stats.Median)});

			if (stats.Active > 0)
			{
				const DayTotals& cheapest = model.Days[stats.Cheapest];
				const DayTotals& dearest = model.Days[stats.Dearest];
				costs.Rows.push_back({"Cheapest", TipMoney(cheapest.Cost), ShortDate(cheapest.Day)});
				costs.Rows.push_back({"Most expensive", TipMoney(dearest.Cost), ShortDate(dearest.Day)});
			}

			card.Sections.push_back(std::move(costs));

			TipSection days;
			days.Rows.push_back({"Active days", TipCount(stats.Active)});
			days.Rows.push_back({"Days without Claude", TipCount(stats.Idle)});
			card.Sections.push_back(std::move(days));
			return card;
		}

		TipCard SessionsTip(const DashboardModel& model)
		{
			TipCard card{.Head = TipCounted(model.Sessions.size(), "session", "sessions")};

			std::vector<double> costs;
			std::vector<double> minutes;
			for (const SessionFacts& session : model.Sessions)
			{
				costs.push_back(session.Cost);
				minutes.push_back(static_cast<double>(ActiveTimeOf(session).count()));
			}

			const std::chrono::minutes medianActiveTime{std::lround(Median(std::move(minutes)))};

			TipSection counts;
			counts.Rows.push_back({"Prompts you sent", TipCount(model.Prompts)});
			counts.Rows.push_back({"Requests to the API", TipCount(model.Requests)});
			counts.Rows.push_back({"Requests per prompt",
								   DashIfNone(model.Prompts, TipRounded(Per(static_cast<double>(model.Requests), model.Prompts)))});
			counts.Rows.push_back({"Median cost", TipMoney(Median(std::move(costs)))});
			counts.Rows.push_back({"Median active time", FormatDuration(medianActiveTime)});
			counts.Rows.push_back({"Compactions", TipCount(model.Compactions)});
			card.Sections.push_back(std::move(counts));

			const auto longest = std::max_element(model.Sessions.begin(), model.Sessions.end(),
												  [](const SessionFacts& left, const SessionFacts& right) {
													  return ActiveTimeOf(left) < ActiveTimeOf(right);
												  });

			if (longest != model.Sessions.end())
			{
				TipSection section{.Title = "Longest at work"};
				section.Rows.push_back({SessionTitle(*longest), FormatDuration(ActiveTimeOf(*longest))});
				card.Sections.push_back(std::move(section));
			}

			CloseTip(card, "Active time adds up the stretches with less than 10 minutes between records; a session left "
						   "open overnight does not count the night.");
			return card;
		}

		TipCard SubagentsTip(const DashboardModel& model)
		{
			TipCard card{.Head = "Subagents", .HeadValue = TipMoney(model.SubagentCost)};

			TipSection runs;
			runs.Rows.push_back({"Runs", TipCount(model.SubagentRuns)});
			runs.Rows.push_back({"Average run", DashIfNone(model.SubagentRuns, TipMoney(Per(model.SubagentCost, model.SubagentRuns)))});
			runs.Rows.push_back({"Share of the cost", TipShare(model.SubagentCost, model.Cost)});
			card.Sections.push_back(std::move(runs));

			card.Sections.push_back(ByModel(model, "By model",
											ColumnOf(model, [](const FamilyTotals& family) { return family.SubagentCost; },
													 Requesters::Subagents, Amount::Money),
											model.SubagentCost, TipMoney));

			std::vector<const SessionFacts*> users;
			for (const SessionFacts& session : model.Sessions)
			{
				if (session.SubagentCost > 0.0)
				{
					users.push_back(&session);
				}
			}

			std::stable_sort(users.begin(), users.end(), [](const SessionFacts* left, const SessionFacts* right) {
				return left->SubagentCost > right->SubagentCost;
			});

			TipSection most{.Title = "Sessions that used them most"};
			for (std::size_t index = 0; index < std::min(kTopSubagentSessions, users.size()); ++index)
			{
				const SessionFacts& session = *users[index];
				most.Rows.push_back({SessionTitle(session), TipMoney(session.SubagentCost),
									 TipShare(session.SubagentCost, session.Cost) + " of it"});
			}

			if (!most.Rows.empty())
			{
				card.Sections.push_back(std::move(most));
			}

			return card;
		}

		TipCard CacheSavedTip(const DashboardModel& model)
		{
			const TokenTotals& tokens = model.Tokens;
			const auto input = static_cast<double>(tokens.CacheRead + tokens.CacheWrite + tokens.Input);
			TipCard card{.Head = "What the cache saved", .HeadValue = TipMoney(model.Saved)};

			TipSection parts;
			parts.Rows.push_back(
				{"Read from cache", FormatTokens(tokens.CacheRead), TipShare(static_cast<double>(tokens.CacheRead), input)});
			parts.Rows.push_back({"Written to cache", FormatTokens(tokens.CacheWrite)});
			parts.Rows.push_back({"Uncached input", FormatTokens(tokens.Input)});
			card.Sections.push_back(std::move(parts));

			card.Sections.push_back(ByModel(model, "Saved by model",
											ColumnOf(model, [](const FamilyTotals& family) { return family.Saved; },
													 Requesters::Everyone, Amount::Money),
											model.Saved, TipMoney));

			CloseTip(card, "Cache reads cost less than input and cache writes cost more; the saving is what these requests "
						   "would have cost with every cached token charged as plain input, less what they cost.");
			return card;
		}

		TipCard OutputTip(const DashboardModel& model)
		{
			const auto output = static_cast<double>(model.Tokens.Output);
			TipCard card{.Head = "Tokens Claude wrote", .HeadValue = FormatTokens(model.Tokens.Output)};

			card.Sections.push_back(ByModel(model, {},
											ColumnOf(model, [](const FamilyTotals& family) { return family.Tokens.Output; },
													 Requesters::Everyone, Amount::Tokens),
											output, TipTokens));

			TipSection rates;
			rates.Rows.push_back({"Per request", DashIfNone(model.Requests, TipTokens(Per(output, model.Requests)))});
			rates.Rows.push_back({"Per prompt you sent", DashIfNone(model.Prompts, TipTokens(Per(output, model.Prompts)))});
			card.Sections.push_back(std::move(rates));
			return card;
		}

		TipCard BusiestDayTip(const DashboardModel& model)
		{
			if (const std::optional<std::size_t> busiest = BusiestDay(model))
			{
				return DayTip(model, *busiest);
			}

			TipCard card{.Head = "Busiest day"};
			card.Notes.push_back("No day has any cost yet.");
			return card;
		}

		TipCard TileCard(const DashboardModel& model, NumberTile tile)
		{
			switch (tile)
			{
			case NumberTile::Spent:
				return SpentTip(model);
			case NumberTile::ThisWeek:
				return ThisWeekTip(model);
			case NumberTile::PerActiveDay:
				return PerActiveDayTip(model);
			case NumberTile::Sessions:
				return SessionsTip(model);
			case NumberTile::Subagents:
				return SubagentsTip(model);
			case NumberTile::CacheSaved:
				return CacheSavedTip(model);
			case NumberTile::Output:
				return OutputTip(model);
			case NumberTile::BusiestDay:
				return BusiestDayTip(model);
			}

			return {};
		}

		std::string TileValueOn(NumberTile tile, const DayTotals& day)
		{
			switch (tile)
			{
			case NumberTile::PerActiveDay:
				return day.Cost > 0.0 ? TipMoney(day.Cost) : "no activity";
			case NumberTile::Sessions:
				return TipCount(day.SessionsStarted) + " started";
			case NumberTile::Subagents:
				return (day.Cost > 0.0 ? TipShare(day.SubagentCost, day.Cost) : std::string{"0%"}) + " of the day";
			case NumberTile::CacheSaved:
				return TipMoney(day.Saved);
			case NumberTile::Output:
				return FormatTokens(day.Output);
			case NumberTile::Spent:
			case NumberTile::ThisWeek:
			case NumberTile::BusiestDay:
				break;
			}

			return TipMoney(day.Cost);
		}

		TipSection SessionsActiveSection(const DashboardModel& model, const DayTotals& day,
										 const std::vector<std::size_t>& active)
		{
			TipSection section{.Title = std::format("Sessions active that day{}{}", kMiddleDotSeparator, active.size())};

			for (std::size_t index = 0; index < std::min(kListedSessions, active.size()); ++index)
			{
				const SessionFacts& session = model.Sessions[active[index]];
				const auto cost = session.CostByDay.find(day.Day);
				section.Rows.push_back({SessionTitle(session), TipMoney(cost == session.CostByDay.end() ? 0.0 : cost->second),
										PlaceLabel(session.Place)});
			}

			section.Notes.push_back(active.size() > kListedSessions
										? std::format("and {} more{}click to list them", active.size() - kListedSessions,
													  kMiddleDotSeparator)
										: std::string{"Click to list them"});
			return section;
		}
	}

	TipCard NumberTip(const DashboardModel& model, NumberTile tile, std::optional<std::size_t> hoveredDay)
	{
		TipCard card = TileCard(model, tile);

		if (hoveredDay && *hoveredDay < model.Days.size())
		{
			const DayTotals& day = model.Days[*hoveredDay];
			card.Banner = WeekdayDate(day.Day) + ": " + TileValueOn(tile, day);
		}

		return card;
	}

	TipCard DayTip(const DashboardModel& model, std::size_t dayIndex)
	{
		if (dayIndex >= model.Days.size())
		{
			return {};
		}

		const DayTotals& day = model.Days[dayIndex];
		TipCard card{.Head = FullDate(day.Day), .HeadValue = TipMoney(day.Cost)};

		const ActiveDayStats stats = ActiveDays(model);
		if (stats.Average > 0.0 && day.Cost > 0.0)
		{
			card.Notes.push_back(Ratio(day.Cost, stats.Average));
		}

		TipSection costs;
		AddFamilyRows(costs.Rows, model, {day.CostByFamily, day.RequestsByFamily, day.UnpricedByFamily}, day.Cost, TipMoney);
		AddThreadRows(costs.Rows, day.Cost, day.SubagentCost);
		NoteUnpriced(costs.Notes, day.UnpricedModels);
		card.Sections.push_back(std::move(costs));

		TipSection counts;
		counts.Rows.push_back({"Prompts you sent", TipCount(day.Prompts)});
		counts.Rows.push_back({"Requests", TipCount(day.Requests)});
		counts.Rows.push_back({"Output tokens", FormatTokens(day.Output)});
		counts.Rows.push_back({"Cache saved", TipMoney(day.Saved)});
		card.Sections.push_back(std::move(counts));

		const std::vector<std::size_t> active = SessionsActiveOn(model, day.Day);
		if (!active.empty())
		{
			card.Sections.push_back(SessionsActiveSection(model, day, active));
		}

		return card;
	}

	TipCard GoneDayTip(std::chrono::year_month_day day)
	{
		TipCard card{.Head = FullDate(day)};
		card.Notes.push_back("No transcript from this day is left on disk. Claude Code deletes transcripts after "
							 "cleanupPeriodDays (30 by default), and Graphite keeps no copy.");
		return card;
	}

	TipCard PunchTip(const DashboardModel& model, int weekday, int hour)
	{
		if (weekday < 0 || weekday >= static_cast<int>(kDaysPerWeek) || hour < 0 || hour >= static_cast<int>(kHoursPerDay))
		{
			return {};
		}

		const HourCell& cell = model.Punch[static_cast<std::size_t>(weekday)][static_cast<std::size_t>(hour)];
		const double weeks = std::max(1.0, std::round(Per(static_cast<double>(model.Days.size()), kDaysPerWeek)));
		TipCard card{.Head = std::format("{} {:02}:00{}{:02}:00", WeekdayAbbreviation(weekday), hour, kEnDash, hour + 1),
					 .HeadValue = TipMoney(cell.Cost)};

		TipSection facts;
		facts.Rows.push_back({"Requests", TipCount(cell.Requests)});
		facts.Rows.push_back({"Share of the cost", TipShare(cell.Cost, model.Cost)});
		facts.Rows.push_back({"Per week, average", TipMoney(cell.Cost / weeks)});
		card.Sections.push_back(std::move(facts));
		return card;
	}

	TipCard CacheTip(const DashboardModel& model, CachePart part)
	{
		switch (part)
		{
		case CachePart::Read:
			return {.Head = "Read from cache",
					.HeadValue = FormatTokens(model.Tokens.CacheRead),
					.Notes = {"Tokens Claude Code sent again that the API had cached: the conversation so far, the system "
							  "prompt and tool definitions. Charged at a tenth of the input price or less."}};
		case CachePart::Written:
			return {.Head = "Written to cache",
					.HeadValue = FormatTokens(model.Tokens.CacheWrite),
					.Notes = {std::format("Tokens put into the cache for the next request; charged at 1.25{0} (5 minutes) or "
										  "2{0} (1 hour) the input price. Main sessions write for 1 hour, subagents for 5 "
										  "minutes.",
										  kMultiplicationSign)}};
		case CachePart::Uncached:
			break;
		}

		return {.Head = "Uncached",
				.HeadValue = FormatTokens(model.Tokens.Input),
				.Notes = {"Input charged at the full price: only the newest part of each request."}};
	}
}
