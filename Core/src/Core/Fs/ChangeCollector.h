#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace Core {

	class ChangeCollector
	{
	public:
		using Clock = std::chrono::steady_clock;

		void Add(const std::filesystem::path& directory, Clock::time_point now);

		void AddOverflow(Clock::time_point now);

		std::vector<std::filesystem::path> TakeSettled(
			Clock::time_point now, std::chrono::milliseconds quietPeriod);

		bool HasOverflowed() const { return m_Overflowed; }

		// When what is pending will have been quiet for `quietPeriod`, if anything is — the
		// earliest a TakeSettled could return it, as long as nothing new arrives.
		std::optional<Clock::time_point> SettlesAt(std::chrono::milliseconds quietPeriod) const;

	private:
		std::set<std::filesystem::path> m_Pending;
		Clock::time_point m_LastChange;
		bool m_PendingOverflow = false;
		bool m_Overflowed = false;
		bool m_HasPending = false;
	};
}