#pragma once

#include "Core/Base/Timestep.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace Core {

	class ChangeCollector
	{
	public:
		using Clock = SteadyClock;

		void Add(const std::filesystem::path& directory, Clock::time_point now);

		void AddOverflow(Clock::time_point now);

		std::vector<std::filesystem::path> TakeSettled(
			Clock::time_point now, std::chrono::milliseconds quietPeriod);

		bool HasOverflowed() const { return m_LastBatchOverflowed; }

		std::optional<Clock::time_point> SettlesAt(std::chrono::milliseconds quietPeriod) const;

	private:
		std::set<std::filesystem::path> m_PendingDirectories;
		Clock::time_point m_LastChangeTime;
		bool m_PendingBatchOverflowed = false;
		bool m_LastBatchOverflowed = false;
		bool m_HasPendingChanges = false;
	};
}
