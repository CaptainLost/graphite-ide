#include "corepch.h"

#include "Core/Fs/ChangeCollector.h"

namespace Core {

	void ChangeCollector::Add(const std::filesystem::path& directory, Clock::time_point now)
	{
		m_Pending.insert(directory);
		m_LastChange = now;
		m_HasPending = true;
	}

	void ChangeCollector::AddOverflow(Clock::time_point now)
	{
		m_PendingOverflow = true;
		m_LastChange = now;
		m_HasPending = true;
	}

	std::vector<std::filesystem::path> ChangeCollector::TakeSettled(
		Clock::time_point now, std::chrono::milliseconds quietPeriod)
	{
		// Cleared on every call, so a consumer that acted on an overflow is not told again on
		// the next frame.
		m_Overflowed = false;

		if (!m_HasPending || now - m_LastChange < quietPeriod)
			return {};

		std::vector<std::filesystem::path> settled(m_Pending.begin(), m_Pending.end());

		m_Pending.clear();
		m_HasPending = false;
		m_Overflowed = m_PendingOverflow;
		m_PendingOverflow = false;

		return settled;
	}

	std::optional<ChangeCollector::Clock::time_point> ChangeCollector::SettlesAt(
		std::chrono::milliseconds quietPeriod) const
	{
		if (!m_HasPending)
			return std::nullopt;

		return m_LastChange + quietPeriod;
	}
}