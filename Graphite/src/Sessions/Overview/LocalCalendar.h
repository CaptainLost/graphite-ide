#pragma once

#include "Sessions/SessionActivity.h"

#include <chrono>
#include <optional>
#include <string_view>

namespace Graphite {

	struct LocalHour
	{
		std::chrono::year_month_day Day;
		int Hour = 0;
		int Weekday = 0; // 0 = Monday

		friend bool operator==(const LocalHour&, const LocalHour&) = default;
	};

	class LocalCalendar
	{
	public:
		// nullptr = UTC
		explicit LocalCalendar(const std::chrono::time_zone* zone);

		LocalHour At(UtcHour hour) const;

		std::optional<std::chrono::year_month_day> DayOf(std::string_view isoTimestamp) const;

		std::chrono::year_month_day Today() const;

		static LocalCalendar Current();

	private:
		std::chrono::local_time<std::chrono::milliseconds> ToLocal(std::chrono::sys_time<std::chrono::milliseconds> instant) const;

		const std::chrono::time_zone* m_Zone = nullptr;
	};
}
