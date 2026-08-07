#include "corepch.h"

#include "Core/Fs/ChangeCollector.h"

namespace core
{
    void ChangeCollector::Add(const std::filesystem::path& directory, Clock::time_point now)
    {
        m_pending.insert(directory);
        m_lastChange = now;
        m_hasPending = true;
    }

    void ChangeCollector::AddOverflow(Clock::time_point now)
    {
        m_pendingOverflow = true;
        m_lastChange = now;
        m_hasPending = true;
    }

    std::vector<std::filesystem::path> ChangeCollector::TakeSettled(
        Clock::time_point now, std::chrono::milliseconds quietPeriod)
    {
        // Cleared on every call, so a consumer that acted on an overflow once is not
        // told again on the next frame.
        m_overflowed = false;

        if (!m_hasPending || now - m_lastChange < quietPeriod)
            return {};

        std::vector<std::filesystem::path> settled(m_pending.begin(), m_pending.end());

        m_pending.clear();
        m_hasPending = false;
        m_overflowed = m_pendingOverflow;
        m_pendingOverflow = false;

        return settled;
    }
}
