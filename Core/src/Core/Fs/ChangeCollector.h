#pragma once

#include <chrono>
#include <filesystem>
#include <set>
#include <vector>

namespace core
{
    /// Collects directories reported as changed and holds them back until the disk has
    /// been quiet for a while. Without that delay a build would trigger thousands of
    /// refreshes and a half-written directory would be read mid-write.
    ///
    /// Time is passed in rather than read, so the rule can be tested without sleeping.
    /// This class does no locking of its own — DirectoryWatcher owns the mutex, because
    /// the lock has to span the surrounding read-modify-write anyway.
    class ChangeCollector
    {
    public:
        using Clock = std::chrono::steady_clock;

        /// Records that `directory` changed. Repeats are free: a directory is reported
        /// once however many entries inside it moved.
        void Add(const std::filesystem::path& directory, Clock::time_point now);

        /// Records that changes were lost — the OS buffer overflowed and the individual
        /// paths are gone. The consumer has to refresh everything it holds.
        void AddOverflow(Clock::time_point now);

        /// The changed directories, if nothing new has arrived for `quietPeriod`;
        /// otherwise empty, leaving them pending. Clears what it returns.
        [[nodiscard]] std::vector<std::filesystem::path> TakeSettled(
            Clock::time_point now, std::chrono::milliseconds quietPeriod);

        /// True when the last TakeSettled call released a lost-changes marker. Reading it
        /// does not clear it; the next TakeSettled does.
        [[nodiscard]] bool Overflowed() const noexcept { return m_overflowed; }

    private:
        /// A set, not a vector: a single build touches one directory thousands of times
        /// and the consumer refreshes each one once.
        std::set<std::filesystem::path> m_pending;
        Clock::time_point m_lastChange;
        bool m_pendingOverflow = false;
        bool m_overflowed = false;
        bool m_hasPending = false;
    };
}
