#include "corepch.h"

#include "Core/Time/TimeZone.h"

#include <exception>

namespace Core {

	const std::chrono::time_zone* LocalTimeZone()
	{
		try
		{
			return std::chrono::current_zone();
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	}

	std::chrono::minutes OffsetAt(const std::chrono::time_zone* zone, std::chrono::sys_seconds instant)
	{
		if (zone == nullptr)
		{
			return std::chrono::minutes{0};
		}

		// WARNING: A zone that was found still asks ICU for its offset at each moment, and that can throw too.
		try
		{
			return std::chrono::floor<std::chrono::minutes>(zone->get_info(instant).offset);
		}
		catch (const std::exception&)
		{
			return std::chrono::minutes{0};
		}
	}
}