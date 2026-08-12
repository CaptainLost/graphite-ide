#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Fs/ChangeCollector.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace Core
{
    /// Watches a directory subtree and reports which directories changed. Runs a thread
    /// that does nothing but record paths; the consumer collects them from its own
    /// thread, so nothing but plain paths crosses the boundary.
    ///
    /// Held through a unique_ptr because the running thread captures `this`.
    ///
    /// Implemented on Windows only; Start returns ErrorCode::Unsupported elsewhere.
    class DirectoryWatcher final : NonCopyable
    {
    public:
        /// How long the disk must be quiet before changes are released.
        static constexpr std::chrono::milliseconds kQuietPeriod{250};

        ~DirectoryWatcher();

        /// Begins watching `root` and everything below it. Fails if the directory cannot
        /// be opened, or if the watch cannot be armed.
        ///
        /// Returns only once the watch is actually armed, so a change made the instant
        /// after it returns is seen. Windows buffers nothing for a directory handle until
        /// a read has been issued against it, and that read happens on the worker thread —
        /// so returning as soon as the handle existed dropped everything that happened
        /// before the worker was first scheduled.
        [[nodiscard]] static Result<std::unique_ptr<DirectoryWatcher>> Start(
            const std::filesystem::path& root);

        /// What one settled batch reports.
        struct Changes
        {
            /// The directories that changed. Empty while the disk is still busy, and
            /// usually empty when `overflowed` is set — though not necessarily, since a
            /// batch can carry paths recorded before the overflow happened. Treat
            /// `overflowed` as covering these rather than excluding them.
            std::vector<std::filesystem::path> directories;

            /// Individual paths were dropped and everything held should be refreshed.
            bool overflowed = false;

            /// Set when the watched directory itself was renamed or moved, to wherever it
            /// is now. The directories above are spelled under the *old* location and are
            /// meaningless once this is set — handle the move and discard them.
            std::optional<std::filesystem::path> movedTo;
        };

        /// The changes settled since the last call, once the disk has been quiet for
        /// kQuietPeriod. Safe to call every frame.
        ///
        /// The fields come back together on purpose. They were once two calls, and the
        /// first consumer written against that API returned early on an empty vector and
        /// so could never observe an overflow — which is the one case that reports no
        /// paths at all. A caller cannot make that mistake with a single result.
        [[nodiscard]] Changes TakeSettled();

        /// False once the watching thread has given up — the directory was deleted, or
        /// the OS refused a notification. From that point TakeSettled reports nothing
        /// ever again, which is indistinguishable from a quiet disk, so a consumer that
        /// cares has to ask.
        [[nodiscard]] bool IsRunning() const noexcept { return m_Running.load(); }

    private:
        DirectoryWatcher() = default;

        void Run();

        /// Asks the open handle where it currently lives. Empty when it cannot be asked.
        [[nodiscard]] std::filesystem::path QueryHandlePath() const;

        void CheckForMove();

        /// CheckForMove, but only once per kMoveCheckInterval. Called on every return
        /// from the wait rather than only on expiry, so a directory too busy to ever let
        /// the wait expire is still checked.
        void CheckForMoveIfDue();
        void RecordChange(const std::filesystem::path& path);

        std::filesystem::path m_Root;
        std::thread m_Thread;
        std::mutex m_Mutex;              // guards m_Collector and m_MovedTo
        ChangeCollector m_Collector;

        /// Atomic rather than under the mutex: the reader is a UI frame asking a yes/no
        /// question, and making it queue behind the worker's writes would be paying for
        /// a guarantee it does not need.
        std::atomic<bool> m_Running{true};

        /// Satisfied by the worker exactly once: with success as soon as the first read
        /// has been issued, and with an error on every path that leaves the worker before
        /// that. Start waits on it, so the error is what the caller gets back instead of a
        /// live-looking watcher whose thread has already died.
        std::promise<Result<void>> m_Armed;

        /// How often the worker asks its handle where it now lives. A rename produces no
        /// notification of any kind, so this poll is the only way to find out — but the
        /// thread is already waiting, so the cost is a wait that expires rather than any
        /// extra work.
        static constexpr std::chrono::milliseconds kMoveCheckInterval{2000};

        /// The path the handle reported at startup, which is the baseline a later reading
        /// is compared against. Deliberately not m_Root: the filesystem stores its own
        /// spelling of the name, so `d:\work` opened by the user comes back as `D:\Work`
        /// and every session would begin by declaring a move.
        std::filesystem::path m_WatchedPath;

        /// When the handle was last asked. Worker-only, like m_WatchedPath.
        ChangeCollector::Clock::time_point m_LastMoveCheck;

        /// Guarded by m_Mutex alongside the collector.
        std::optional<std::filesystem::path> m_MovedTo;

        /// Opaque on purpose: these are Win32 HANDLEs, and the header must not drag
        /// windows.h into every translation unit that watches a directory.
        void* m_Directory = nullptr;
        void* m_StopEvent = nullptr;
    };
}
