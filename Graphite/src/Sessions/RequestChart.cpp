#include "Core/Base/Base.h"

#include "Sessions/RequestChart.h"

#include <format>

namespace Graphite {

	ContextLayers LayersOf(const ApiRequest& request)
	{
		const TokenCounts& usage = request.Usage;

		ContextLayers layers{};
		layers[static_cast<std::size_t>(ContextLayer::CacheRead)] = static_cast<double>(usage.CacheRead);
		layers[static_cast<std::size_t>(ContextLayer::CacheWrite)] =
			static_cast<double>(usage.CacheWrite5m + usage.CacheWrite1h);
		layers[static_cast<std::size_t>(ContextLayer::Uncached)] = static_cast<double>(usage.Input);
		return layers;
	}

	ContextLayers StackedTops(const ContextLayers& layers, const std::array<bool, kContextLayers>& shown)
	{
		ContextLayers tops{};
		double below = 0.0;

		for (std::size_t layer = 0; layer < kContextLayers; ++layer)
		{
			if (shown[layer])
				below += layers[layer];

			tops[layer] = below;
		}

		return tops;
	}

	std::string FormatLocalTime(std::chrono::local_seconds time, ClockStyle style)
	{
		// English names whatever the reader's locale, like the rest of the interface.
		static constexpr const char* kWeekdays[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
													"Thursday", "Friday", "Saturday"};

		const auto day = std::chrono::floor<std::chrono::days>(time);
		const std::chrono::year_month_day date{day};
		const std::chrono::hh_mm_ss clock{time - day};

		const std::string hms = std::format("{:02}:{:02}:{:02}", clock.hours().count(), clock.minutes().count(),
											clock.seconds().count());
		const int year = static_cast<int>(date.year());
		const unsigned month = static_cast<unsigned>(date.month());
		const unsigned dayOfMonth = static_cast<unsigned>(date.day());

		switch (style)
		{
		case ClockStyle::Time: return hms;
		case ClockStyle::MonthDayTime: return std::format("{:02}-{:02} {}", month, dayOfMonth, hms);
		case ClockStyle::DateTime: return std::format("{:04}-{:02}-{:02} {}", year, month, dayOfMonth, hms);
		case ClockStyle::DateTimeWeekday:
			return std::format("{:04}-{:02}-{:02} {} ({})", year, month, dayOfMonth, hms,
							   kWeekdays[std::chrono::weekday{day}.c_encoding()]);
		}

		return hms;
	}
}
