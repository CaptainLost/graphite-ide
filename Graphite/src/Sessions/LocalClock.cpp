#include "Sessions/LocalClock.h"

#include "Core/Base/Base.h"
#include "Core/Time/TimeZone.h"

#include "Sessions/SessionSummary.h"

#include <exception>
#include <format>

namespace Graphite {

	std::chrono::minutes LocalClock::OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant)
	{
		if (!m_ZoneResolved)
		{
			m_ZoneResolved = true;
			m_Zone = Core::LocalTimeZone();
		}

		// Without time zone data the times read as UTC rather than the view failing to draw.
		if (m_Zone == nullptr)
		{
			return std::chrono::minutes{0};
		}

		const bool cachedSpanCoversInstant =
			m_CachedZoneInfo.has_value() && instant >= m_CachedZoneInfo->begin && instant < m_CachedZoneInfo->end;

		if (!cachedSpanCoversInstant)
		{
			try
			{
				m_CachedZoneInfo = m_Zone->get_info(std::chrono::floor<std::chrono::seconds>(instant));
			}
			catch (const std::exception&)
			{
				m_CachedZoneInfo.reset();
				return std::chrono::minutes{0};
			}
		}

		return std::chrono::floor<std::chrono::minutes>(m_CachedZoneInfo->offset);
	}

	std::string LocalClock::HoursMinutes(const std::string& timestamp)
	{
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> parsedInstant = ParseTimestamp(timestamp);

		if (!parsedInstant)
		{
			return {};
		}

		const std::chrono::sys_time<std::chrono::minutes> localMinutes =
			std::chrono::floor<std::chrono::minutes>(*parsedInstant) + OffsetAt(*parsedInstant);
		const std::chrono::hh_mm_ss timeOfDay{localMinutes - std::chrono::floor<std::chrono::days>(localMinutes)};

		return std::format("{:02}:{:02}", timeOfDay.hours().count(), timeOfDay.minutes().count());
	}

	std::optional<std::chrono::local_seconds> LocalClock::LocalTime(const std::string& timestamp)
	{
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> parsedInstant = ParseTimestamp(timestamp);

		if (!parsedInstant)
		{
			return std::nullopt;
		}

		const std::chrono::sys_time<std::chrono::seconds> shiftedSeconds =
			std::chrono::floor<std::chrono::seconds>(*parsedInstant) + OffsetAt(*parsedInstant);
		return std::chrono::local_seconds{shiftedSeconds.time_since_epoch()};
	}

	std::string LocalClock::Format(const std::string& timestamp, ClockStyle style)
	{
		const std::optional<std::chrono::local_seconds> localTime = LocalTime(timestamp);
		return localTime ? FormatLocalTime(*localTime, style) : std::string{};
	}

	std::string LocalClock::HoursMinutesSeconds(const std::string& timestamp)
	{
		return Format(timestamp, ClockStyle::Time);
	}
}
