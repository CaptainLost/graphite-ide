#include "Sessions/Overview/LocalCalendar.h"

#include "Sessions/SessionSummary.h"

namespace Graphite {

	LocalCalendar::LocalCalendar(const std::chrono::time_zone* zone)
		: m_Zone(zone)
	{
	}

	std::chrono::local_time<std::chrono::milliseconds> LocalCalendar::ToLocal(
		std::chrono::sys_time<std::chrono::milliseconds> instant) const
	{
		if (m_Zone == nullptr)
			return std::chrono::local_time<std::chrono::milliseconds>{instant.time_since_epoch()};
		return m_Zone->to_local(instant);
	}

	LocalHour LocalCalendar::At(UtcHour hour) const
	{
		const auto local = ToLocal(std::chrono::time_point_cast<std::chrono::milliseconds>(hour));
		const auto days = std::chrono::floor<std::chrono::days>(local);
		const std::chrono::year_month_day day{days};
		const std::chrono::hh_mm_ss<std::chrono::milliseconds> time{local - days};
		const std::chrono::weekday weekday{days};
		return {day, static_cast<int>(time.hours().count()), static_cast<int>((weekday.c_encoding() + 6) % 7)};
	}

	std::optional<std::chrono::year_month_day> LocalCalendar::DayOf(std::string_view isoTimestamp) const
	{
		const auto instant = ParseTimestamp(isoTimestamp);
		if (!instant)
			return std::nullopt;
		return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(ToLocal(*instant))};
	}

	std::chrono::year_month_day LocalCalendar::Today() const
	{
		const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
		return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(ToLocal(now))};
	}

	LocalCalendar LocalCalendar::Current()
	{
		return LocalCalendar{std::chrono::current_zone()};
	}
}
