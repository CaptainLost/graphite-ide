#include "Sessions/Overview/TipText.h"

#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include "Core/Base/Count.h"
#include "Core/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace Graphite {

	namespace {
		constexpr std::array<std::string_view, 7> kWeekdays = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
		constexpr std::array<std::string_view, 12> kMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
															  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	}

	std::string TipMoney(double dollars)
	{
		return FormatCost(CostResult{.Dollars = dollars});
	}

	std::string TipCount(std::size_t count)
	{
		return Core::FormatCount(count);
	}

	std::string TipCounted(std::size_t count, std::string_view one, std::string_view many)
	{
		return std::format("{} {}", TipCount(count), count == 1 ? one : many);
	}

	std::string TipRounded(double value)
	{
		return TipCount(static_cast<std::size_t>(std::max(0.0, std::round(value))));
	}

	std::string TipTokens(double tokens)
	{
		return FormatTokens(static_cast<std::uint64_t>(std::max(0.0, std::round(tokens))));
	}

	std::string TipShare(double part, double whole)
	{
		if (whole <= 0.0)
		{
			return std::string{kEnDash};
		}

		const double percent = 100.0 * part / whole;

		if (percent > 0.0 && percent < 1.0)
		{
			return std::format("{:.1f}%", percent);
		}

		return std::format("{}%", std::lround(percent));
	}

	std::string_view WeekdayAbbreviation(int mondayBased)
	{
		return kWeekdays[static_cast<std::size_t>(std::clamp(mondayBased, 0, static_cast<int>(kWeekdays.size()) - 1))];
	}

	std::string ShortDate(std::chrono::year_month_day day)
	{
		return std::format("{} {}", static_cast<unsigned>(day.day()), kMonths[static_cast<unsigned>(day.month()) - 1]);
	}

	std::string WeekdayDate(std::chrono::year_month_day day)
	{
		const std::chrono::weekday weekday{std::chrono::sys_days{day}};
		return std::format("{} {}", WeekdayAbbreviation(static_cast<int>(weekday.iso_encoding()) - 1), ShortDate(day));
	}

	std::string FullDate(std::chrono::year_month_day day)
	{
		return std::format("{} {}", WeekdayDate(day), static_cast<int>(day.year()));
	}

	std::string ProjectName(const std::filesystem::path& repository)
	{
		const std::filesystem::path folder = repository.filename().empty() && repository.has_relative_path()
												 ? repository.parent_path()
												 : repository;
		const std::string name = Core::ToUtf8(folder.filename());
		return name.empty() ? Core::ToUtf8(repository) : name;
	}

	std::string PlaceLabel(const SessionPlace& place)
	{
		std::string label = ProjectName(place.Repository);

		if (!place.Worktree.empty())
		{
			label += kMiddleDotSeparator;
			label += place.Worktree;
		}

		return label;
	}

	std::string SessionTitle(const SessionFacts& session, std::size_t maxBytes)
	{
		return session.Summary == nullptr ? std::string{} : SingleLineLabel(session.Summary->DisplayTitle(), maxBytes);
	}

	std::string FamilyLabel(const DashboardModel& model, ModelFamily family)
	{
		const std::size_t index = std::min(static_cast<std::size_t>(family), kModelFamilyCount - 1);
		const FamilyTotals& totals = model.Families[index];

		if (family == ModelFamily::Other && totals.RequestsByModel.size() == 1 && totals.UnpricedModels.size() == 1)
		{
			return ModelLabel(*totals.UnpricedModels.begin(), false);
		}

		return std::string{FamilyName(family)};
	}

	std::string SessionMoney(const SessionFacts& session)
	{
		std::string money = TipMoney(session.Cost);

		if (session.UnpricedRequests > 0)
		{
			money += ' ';
			money += kUnknownAmount;
		}

		return money;
	}

	void AddFamilyRows(std::vector<TipRow>& rows, const DashboardModel& model, const FamilyColumn& column, double total,
					   std::string (*format)(double))
	{
		for (std::size_t index = 0; index < kModelFamilyCount; ++index)
		{
			if (column.Requests[index] == 0 && column.Amounts[index] == 0.0)
			{
				continue;
			}

			const auto family = static_cast<ModelFamily>(index);
			const bool unpriced = column.Unpriced[index] > 0;
			rows.push_back({FamilyLabel(model, family), unpriced ? std::string{kUnknownAmount} : format(column.Amounts[index]),
							unpriced ? std::string{} : TipShare(column.Amounts[index], total), TipTone::Dim, family});
		}
	}

	void NoteUnpriced(std::vector<std::string>& notes, const std::set<std::string>& models)
	{
		if (models.empty())
		{
			return;
		}

		notes.push_back(UnknownCostHint(CostResult{.UnknownModels = {models.begin(), models.end()}}));
	}

	void CloseTip(TipCard& card, std::string note)
	{
		if (card.Sections.empty())
		{
			card.Notes.push_back(std::move(note));
			return;
		}

		card.Sections.back().Notes.push_back(std::move(note));
	}
}
