#include "corepch.h"

#include "Core/FileSystem/ChangeCollector.h"

namespace Core {

	void ChangeCollector::Add(const std::filesystem::path& directory, Clock::time_point now)
	{
		m_PendingDirectories.insert(directory);
		m_LastChangeTime = now;
		m_HasPendingChanges = true;
	}

	void ChangeCollector::AddOverflow(Clock::time_point now)
	{
		m_PendingBatchOverflowed = true;
		m_LastChangeTime = now;
		m_HasPendingChanges = true;
	}

	std::vector<std::filesystem::path> ChangeCollector::TakeSettled(
		Clock::time_point now, std::chrono::milliseconds quietPeriod)
	{
		m_LastBatchOverflowed = false;

		const bool stillWithinQuietPeriod = now - m_LastChangeTime < quietPeriod;

		if (!m_HasPendingChanges || stillWithinQuietPeriod)
		{
			return {};
		}

		std::vector<std::filesystem::path> settled(m_PendingDirectories.begin(), m_PendingDirectories.end());

		m_PendingDirectories.clear();
		m_HasPendingChanges = false;
		m_LastBatchOverflowed = m_PendingBatchOverflowed;
		m_PendingBatchOverflowed = false;

		return settled;
	}

	std::optional<ChangeCollector::Clock::time_point> ChangeCollector::SettlesAt(
		std::chrono::milliseconds quietPeriod) const
	{
		if (!m_HasPendingChanges)
		{
			return std::nullopt;
		}

		return m_LastChangeTime + quietPeriod;
	}
}
