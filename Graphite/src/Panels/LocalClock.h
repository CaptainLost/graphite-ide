#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace Graphite {

	// A transcript's UTC timestamps as the reader's wall clock shows them. Shared by the
	// session views so each keeps one time zone lookup rather than one per row.
	class LocalClock
	{
	public:
		// "HH:MM" in the reader's time zone, or nothing when the timestamp does not parse.
		std::string HoursMinutes(const std::string& timestamp);

		// "HH:MM:SS" in the reader's time zone, or nothing when the timestamp does not parse.
		std::string HoursMinutesSeconds(const std::string& timestamp);

	private:
		// The reader's offset from UTC at `instant`; zero when the time zone is unknown.
		std::chrono::minutes OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant);

		// The reader's time zone, looked up once and null when it could not be, and the span
		// of time the offset last asked of it holds for.
		const std::chrono::time_zone* m_Zone = nullptr;
		bool m_ZoneResolved = false;
		std::optional<std::chrono::sys_info> m_ZoneInfo;
	};
}
