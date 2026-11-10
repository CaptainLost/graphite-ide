#pragma once

#include "Sessions/Overview/DashboardTips.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	inline constexpr std::size_t kTipLabelBytes = 300;

	std::string TipMoney(double dollars);

	std::string TipCount(std::size_t count);

	std::string TipCounted(std::size_t count, std::string_view one, std::string_view many); // "1 call", "12 calls"

	std::string TipRounded(double value);

	std::string TipTokens(double tokens);

	std::string TipShare(double part, double whole); // "12%", "0.4%", a dash when whole is nothing

	std::string_view WeekdayAbbreviation(int mondayBased); // "Mon"

	std::string ShortDate(std::chrono::year_month_day day); // "9 Nov"

	std::string WeekdayDate(std::chrono::year_month_day day); // "Mon 9 Nov"

	std::string FullDate(std::chrono::year_month_day day); // "Mon 9 Nov 2026"

	std::string ProjectName(const std::filesystem::path& repository);

	std::string PlaceLabel(const SessionPlace& place); // "IDE · fox"

	std::string SessionTitle(const SessionFacts& session, std::size_t maxBytes = kTipLabelBytes);

	void AddFamilyRows(std::vector<TipRow>& rows, const FamilyCosts& amounts, double total, std::string (*format)(double));

	void CloseTip(TipCard& card, std::string note);
}
