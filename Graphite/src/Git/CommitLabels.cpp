#include "Git/CommitLabels.h"

#include <cctype>
#include <format>
#include <string_view>

namespace Graphite {

	namespace {

		std::string Count(long long value, std::string_view unit)
		{
			return value == 1 ? std::format("1 {} ago", unit)
							  : std::format("{} {}s ago", value, unit);
		}

		char UpperFirst(std::string_view word)
		{
			return word.empty()
					   ? '?'
					   : static_cast<char>(std::toupper(static_cast<unsigned char>(word.front())));
		}
	}

	std::string RelativeAge(std::chrono::sys_seconds when, std::chrono::sys_seconds now)
	{
		const auto elapsed = now - when;

		if (elapsed < std::chrono::seconds{60})
		{
			return "just now";
		}

		const long long seconds = elapsed.count();

		if (seconds < 3600)
		{
			return Count(seconds / 60, "minute");
		}

		if (seconds < 86400)
		{
			return Count(seconds / 3600, "hour");
		}

		if (seconds < 2592000)
		{
			return Count(seconds / 86400, "day");
		}

		if (seconds < 31536000)
		{
			return Count(seconds / 2592000, "month");
		}

		return Count(seconds / 31536000, "year");
	}

	std::string AuthorInitials(const Core::Signature& who)
	{
		if (who.Name.empty())
		{
			return std::string{UpperFirst(who.Email)};
		}

		const std::string_view name = who.Name;

		const std::size_t firstEnd = name.find(' ');

		if (firstEnd == std::string_view::npos)
		{
			return std::string{UpperFirst(name)};
		}

		const std::size_t lastStart = name.find_last_of(' ') + 1;

		return std::string{UpperFirst(name)} + std::string{UpperFirst(name.substr(lastStart))};
	}
}
