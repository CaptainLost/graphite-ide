#pragma once

#include <chrono>
#include <filesystem>
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

	private:
		std::set<std::filesystem::path> m_Pending;
		Clock::time_point m_LastChange;
		bool m_PendingOverflow = false;
		bool m_Overflowed = false;
		bool m_HasPending = false;
	};
}