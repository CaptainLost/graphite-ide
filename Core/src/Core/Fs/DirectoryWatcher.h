#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Fs/ChangeCollector.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace core
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
        /// be opened.
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
        };

        /// The changes settled since the last call, once the disk has been quiet for
        /// kQuietPeriod. Safe to call every frame.
        ///
        /// Both fields come back together on purpose. They were once two calls, and the
        /// first consumer written against that API returned early on an empty vector and
        /// so could never observe an overflow — which is the one case that reports no
        /// paths at all. A caller cannot make that mistake with a single result.
        [[nodiscard]] Changes TakeSettled();

    private:
        DirectoryWatcher() = default;

        void Run();
        void RecordChange(const std::filesystem::path& path);

        std::filesystem::path m_root;
        std::thread m_thread;
        std::mutex m_mutex;              // guards m_collector
        ChangeCollector m_collector;

        /// Opaque on purpose: these are Win32 HANDLEs, and the header must not drag
        /// windows.h into every translation unit that watches a directory.
        void* m_directory = nullptr;
        void* m_stopEvent = nullptr;
    };
}
