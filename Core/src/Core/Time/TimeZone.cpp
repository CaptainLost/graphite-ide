#include "corepch.h"

#include "Core/Time/TimeZone.h"

#include <cstddef>
#include <exception>
#include <vector>

namespace Core {

	namespace {

		// Plenty for every daylight-saving period a long history spans; past it the memo starts over rather than grow.
		constexpr std::size_t kMostSpansKept = 256;

		struct KnownSpan
		{
			const std::chrono::time_zone* Zone = nullptr;
			std::chrono::sys_seconds Begin{};
			std::chrono::sys_seconds End{};
			std::chrono::minutes Offset{};
		};

		const KnownSpan* FindSpan(const std::vector<KnownSpan>& known, const std::chrono::time_zone* zone,
								  std::chrono::sys_seconds instant)
		{
			for (const KnownSpan& span : known)
			{
				if (span.Zone == zone && instant >= span.Begin && instant < span.End)
				{
					return &span;
				}
			}

			return nullptr;
		}
	}

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

		// WARNING: Kept per thread, so a caller on any thread may ask without a lock. A zone's spans do not change while
		// the process runs, so nothing kept goes stale.
		thread_local std::vector<KnownSpan> known;

		if (const KnownSpan* const span = FindSpan(known, zone, instant))
		{
			return span->Offset;
		}

		// WARNING: A zone that was found still asks ICU for its offset at each moment, and that can throw too.
		try
		{
			const std::chrono::sys_info info = zone->get_info(instant);
			const std::chrono::minutes offset = std::chrono::floor<std::chrono::minutes>(info.offset);

			if (known.size() >= kMostSpansKept)
			{
				known.clear();
			}

			known.push_back(KnownSpan{.Zone = zone, .Begin = info.begin, .End = info.end, .Offset = offset});
			return offset;
		}
		catch (const std::exception&)
		{
			return std::chrono::minutes{0};
		}
	}
}