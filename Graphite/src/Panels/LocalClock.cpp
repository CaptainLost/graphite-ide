#include "Core/Base/Base.h"

#include "Panels/LocalClock.h"

#include "Sessions/SessionSummary.h"

#include <exception>
#include <format>

namespace Graphite {

	std::chrono::minutes LocalClock::OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant)
	{
		// The zone is looked up once. The lookup throws when the time zone database is not
		// available, which happens on a Windows install without the ICU data chrono reads:
		// the times then fall back to UTC rather than the view failing to draw.
		if (!m_ZoneResolved)
		{
			m_ZoneResolved = true;

			try
			{
				m_Zone = std::chrono::current_zone();
			}
			catch (const std::exception&)
			{
				m_Zone = nullptr;
			}
		}

		if (m_Zone == nullptr)
			return std::chrono::minutes{0};

		// The offset at the instant itself, so a time on the far side of a daylight saving
		// change is on the clock it was then. The zone's answer holds for a whole span, which
		// nearly every time in a session falls within, so it is asked again only outside it.
		if (!m_ZoneInfo || instant < m_ZoneInfo->begin || instant >= m_ZoneInfo->end)
		{
			try
			{
				m_ZoneInfo = m_Zone->get_info(std::chrono::floor<std::chrono::seconds>(instant));
			}
			catch (const std::exception&)
			{
				m_ZoneInfo.reset();
				return std::chrono::minutes{0};
			}
		}

		return std::chrono::floor<std::chrono::minutes>(m_ZoneInfo->offset);
	}

	std::string LocalClock::HoursMinutes(const std::string& timestamp)
	{
		const auto parsed = ParseTimestamp(timestamp);

		if (!parsed)
			return {};

		const auto local = std::chrono::floor<std::chrono::minutes>(*parsed) + OffsetAt(*parsed);
		const std::chrono::hh_mm_ss clock{local - std::chrono::floor<std::chrono::days>(local)};

		return std::format("{:02}:{:02}", clock.hours().count(), clock.minutes().count());
	}

	std::string LocalClock::HoursMinutesSeconds(const std::string& timestamp)
	{
		const auto parsed = ParseTimestamp(timestamp);

		if (!parsed)
			return {};

		const auto local = std::chrono::floor<std::chrono::seconds>(*parsed) + OffsetAt(*parsed);
		const std::chrono::hh_mm_ss clock{local - std::chrono::floor<std::chrono::days>(local)};

		return std::format("{:02}:{:02}:{:02}", clock.hours().count(), clock.minutes().count(),
						   clock.seconds().count());
	}
}
