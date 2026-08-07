#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "Core/Base/Log.h"

#ifdef CORE_PLATFORM_WINDOWS
    #include <windows.h>
#endif

namespace core
{
#ifdef CORE_PLATFORM_WINDOWS
    namespace
    {
        /// 64 KB is the documented ceiling for a network path, and the call fails
        /// outright above it. Overflow is handled rather than avoided by a bigger buffer.
        constexpr DWORD kBufferBytes = 64 * 1024;

        constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                  FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

        /// Git rewrites files under .git constantly, so anything inside it is dropped —
        /// but `.git` appearing is how a `git init` in a terminal becomes visible, so the
        /// directory itself passes. Hence "a component other than the last".
        bool IsInsideGitDirectory(const std::filesystem::path& relative)
        {
            auto component = relative.begin();

            if (component == relative.end())
                return false;

            for (auto next = std::next(component); next != relative.end(); ++component, ++next)
            {
                if (*component == ".git")
                    return true;
            }

            return false;
        }
    }
#endif

    DirectoryWatcher::~DirectoryWatcher()
    {
#ifdef CORE_PLATFORM_WINDOWS
        if (m_stopEvent != nullptr)
            ::SetEvent(static_cast<HANDLE>(m_stopEvent));

        if (m_thread.joinable())
            m_thread.join();

        if (m_stopEvent != nullptr)
            ::CloseHandle(static_cast<HANDLE>(m_stopEvent));

        if (m_directory != nullptr)
            ::CloseHandle(static_cast<HANDLE>(m_directory));
#endif
    }

    Result<std::unique_ptr<DirectoryWatcher>> DirectoryWatcher::Start(
        const std::filesystem::path& root)
    {
#ifdef CORE_PLATFORM_WINDOWS
        // FILE_SHARE_DELETE included on purpose: without it, watching a folder would stop
        // anyone from renaming or deleting it while the IDE is open.
        const HANDLE directory = ::CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

        if (directory == INVALID_HANDLE_VALUE)
        {
            return Error{ErrorCode::IoFailure,
                         std::format("cannot watch '{}'", root.string())};
        }

        const HANDLE stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (stopEvent == nullptr)
        {
            ::CloseHandle(directory);
            return Error{ErrorCode::IoFailure, "cannot create the watcher stop event"};
        }

        // Not make_unique: the constructor is private, and this is the only creator.
        std::unique_ptr<DirectoryWatcher> watcher{new DirectoryWatcher};

        watcher->m_root = root;
        watcher->m_directory = directory;
        watcher->m_stopEvent = stopEvent;

        // Taken before the thread starts, so the worker never sees an empty baseline and
        // mistakes it for a move.
        watcher->m_watchedPath = watcher->QueryHandlePath();
        watcher->m_lastMoveCheck = ChangeCollector::Clock::now();

        // Started last: the thread reads the members above, so they must all be set.
        watcher->m_thread = std::thread{[raw = watcher.get()] { raw->Run(); }};

        return watcher;
#else
        (void)root;
        return Error{ErrorCode::Unsupported,
                     "watching a directory is not implemented on this platform"};
#endif
    }

    void DirectoryWatcher::Run()
    {
        // Whatever way this function leaves — an orderly stop or a failure — the thread
        // is gone afterwards and nothing more will be reported.
        struct RunningFlag
        {
            std::atomic<bool>& flag;
            ~RunningFlag() { flag.store(false); }
        } runningFlag{m_running};

#ifdef CORE_PLATFORM_WINDOWS
        // DWORD-aligned as ReadDirectoryChangesW requires; a vector<char> would not be.
        std::vector<DWORD> buffer(kBufferBytes / sizeof(DWORD));

        const HANDLE ioEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (ioEvent == nullptr)
        {
            CORE_ERROR("Cannot create the watcher I/O event");
            return;
        }

        while (true)
        {
            OVERLAPPED overlapped{};
            overlapped.hEvent = ioEvent;
            ::ResetEvent(ioEvent);

            DWORD ignored = 0;

            if (!::ReadDirectoryChangesW(static_cast<HANDLE>(m_directory), buffer.data(),
                                         kBufferBytes, TRUE, kFilter, &ignored, &overlapped,
                                         nullptr))
            {
                CORE_WARN("Stopped watching '{}': the directory is no longer readable",
                          m_root.string());
                break;
            }

            const HANDLE waited[] = {ioEvent, static_cast<HANDLE>(m_stopEvent)};
            DWORD signalled = WAIT_TIMEOUT;

            while (true)
            {
                signalled = ::WaitForMultipleObjects(
                    2, waited, FALSE, static_cast<DWORD>(kMoveCheckInterval.count()));

                // Driven by elapsed time rather than by the wait expiring. A directory
                // busier than the interval never lets it expire — every notification
                // restarts the clock — and that is precisely when a move would go
                // unnoticed, because after one the handle keeps reporting activity from
                // the new location.
                //
                // Skipped while stopping, so a shutdown never waits on an API call that
                // a disconnected network share could sit on.
                if (signalled != WAIT_OBJECT_0 + 1)
                    CheckForMoveIfDue();

                if (signalled != WAIT_TIMEOUT)
                    break;

                // The read is still outstanding and stays that way; this loop only
                // borrows the idle time between notifications.
            }

            if (signalled != WAIT_OBJECT_0)
            {
                // Stopping is the ordinary way out and says nothing; a failed wait is a
                // bug or a closed handle and would otherwise look identical from outside.
                if (signalled == WAIT_FAILED)
                {
                    CORE_WARN("Stopped watching '{}': the change wait failed",
                              m_root.string());
                }

                // Cancel the outstanding read and wait for
                // the kernel to finish with the buffer before it goes out of scope —
                // skipping this is a use-after-free that fires only under load.
                ::CancelIoEx(static_cast<HANDLE>(m_directory), &overlapped);

                DWORD discarded = 0;
                ::GetOverlappedResult(static_cast<HANDLE>(m_directory), &overlapped, &discarded,
                                      TRUE);
                break;
            }

            DWORD bytes = 0;

            if (!::GetOverlappedResult(static_cast<HANDLE>(m_directory), &overlapped, &bytes,
                                       FALSE))
            {
                // An overflow is reported here on some paths and as a zero byte count on
                // others, depending on how the status is surfaced. Both are the same
                // event and neither is fatal — treating this one as a failure would end
                // the loop and silently stop watching for the rest of the session,
                // exactly when the disk is busiest.
                if (::GetLastError() == ERROR_NOTIFY_ENUM_DIR)
                {
                    std::scoped_lock lock{m_mutex};
                    m_collector.AddOverflow(ChangeCollector::Clock::now());
                    continue;
                }

                // Anything else is fatal. Say so: from here the watcher is permanently
                // dead and the consumer just sees nothing ever change again, which is
                // very hard to guess at.
                CORE_WARN("Stopped watching '{}': the change notification failed",
                          m_root.string());
                break;
            }

            if (bytes == 0)
            {
                // The other spelling of the same overflow: the kernel could not keep up
                // and discarded the individual records, so the consumer has to assume
                // everything it holds is stale.
                std::scoped_lock lock{m_mutex};
                m_collector.AddOverflow(ChangeCollector::Clock::now());
                continue;
            }

            // Walked with an explicit offset checked against `bytes` rather than by
            // following NextEntryOffset on trust. The kernel is not expected to hand back
            // a malformed chain, but reading past the buffer on the one day it does is
            // not a failure worth risking for two comparisons.
            const auto* const base = reinterpret_cast<const std::byte*>(buffer.data());

            for (DWORD offset = 0; offset < bytes;)
            {
                const auto* record =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset);

                const DWORD headerBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);

                if (offset + headerBytes + record->FileNameLength > bytes)
                {
                    CORE_WARN("Discarding a truncated change record while watching '{}'",
                              m_root.string());
                    break;
                }

                const std::wstring name(record->FileName,
                                        record->FileNameLength / sizeof(wchar_t));

                RecordChange(m_root / name);

                // Zero terminates the chain; anything that does not move forward would
                // spin here forever, so both end the walk.
                if (record->NextEntryOffset == 0)
                    break;

                offset += record->NextEntryOffset;
            }
        }

        ::CloseHandle(ioEvent);
#endif
    }

    std::filesystem::path DirectoryWatcher::QueryHandlePath() const
    {
#ifdef CORE_PLATFORM_WINDOWS
        // Asked for twice: once for the length, once for the content. The two calls do
        // not agree on what they count — a call that could not fit the path returns the
        // size *including* the terminator, while one that succeeded returns the length
        // *excluding* it. So `length` already has room for the terminator, and a second
        // call reporting `written >= length` means the path grew between the two calls
        // rather than that it just fit. Do not "simplify" either of those.
        const DWORD length =
            ::GetFinalPathNameByHandleW(static_cast<HANDLE>(m_directory), nullptr, 0,
                                        FILE_NAME_NORMALIZED);

        if (length == 0)
            return {};

        std::wstring buffer(length, L'\0');

        const DWORD written = ::GetFinalPathNameByHandleW(
            static_cast<HANDLE>(m_directory), buffer.data(), length, FILE_NAME_NORMALIZED);

        if (written == 0 || written >= length)
            return {};

        buffer.resize(written);

        // The result carries the \\?\ extended-length prefix, which nothing else in the
        // application uses and which would make every comparison fail.
        //
        // A network location — including a mapped drive letter — comes back as
        // \\?\UNC\server\share, where stripping only the first four characters leaves
        // `UNC\server\share`: not an ugly path but a wrong one, naming nothing. It has to
        // become \\server\share instead, so this case is tested first.
        constexpr std::wstring_view kUncPrefix = LR"(\\?\UNC\)";
        constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";

        if (buffer.starts_with(kUncPrefix))
            buffer.replace(0, kUncPrefix.size(), LR"(\\)");
        else if (buffer.starts_with(kExtendedPrefix))
            buffer.erase(0, kExtendedPrefix.size());

        return std::filesystem::path{buffer};
#else
        return {};
#endif
    }

    void DirectoryWatcher::CheckForMove()
    {
#ifdef CORE_PLATFORM_WINDOWS
        const std::filesystem::path current = QueryHandlePath();

        // An unanswerable handle is not a move. It happens while the directory is being
        // deleted, and that path ends the loop by itself through the failing read.
        if (current.empty())
            return;

        // An empty baseline means the query failed back in Start rather than that the
        // directory was somewhere else. Adopt the first answer instead of announcing a
        // move to where we already are, which would cost the workspace a pointless
        // watcher restart and repository reopen.
        if (m_watchedPath.empty())
        {
            m_watchedPath = current;
            m_root = current;
            return;
        }

        if (current == m_watchedPath)
            return;

        CORE_INFO("The watched directory moved from '{}' to '{}'", m_watchedPath.string(),
                  current.string());

        // The baseline advances, so the move is reported once rather than on every poll
        // from here on.
        m_watchedPath = current;

        // So does the root every reported path is built from. Both are touched only by
        // this thread after Start returns, so neither needs the lock.
        m_root = current;

        std::scoped_lock lock{m_mutex};

        // Whatever was already collected is spelled under the old location and matches
        // nothing any more. Dropping it here rather than leaving it to the consumer
        // matters: only the batch carrying `movedTo` is documented as suspect, so
        // anything still inside the quiet period would surface in the *next* batch
        // looking perfectly current.
        m_collector = ChangeCollector{};
        m_movedTo = current;
#endif
    }

    void DirectoryWatcher::CheckForMoveIfDue()
    {
        const auto now = ChangeCollector::Clock::now();

        if (now - m_lastMoveCheck < kMoveCheckInterval)
            return;

        m_lastMoveCheck = now;
        CheckForMove();
    }

    void DirectoryWatcher::RecordChange(const std::filesystem::path& path)
    {
#ifdef CORE_PLATFORM_WINDOWS
        const std::filesystem::path relative = path.lexically_relative(m_root);

        // git keeps a second set of ignore rules here, private to this clone and not
        // committed. It is the one thing under .git worth hearing about, so it is
        // matched before the blanket filter below drops the rest.
        //
        // Reported as the root rather than as the directory holding it: what changed is
        // the verdict for every entry in the tree, and refreshing the root is what
        // re-asks them all.
        //
        // Only reachable when the watched directory is the repository's own root. Open a
        // subdirectory of a repository — or a worktree, whose real git directory lives
        // elsewhere — and this file sits outside the watched subtree, where nothing can
        // see it change.
        if (relative == std::filesystem::path{".git"} / "info" / "exclude")
        {
            std::scoped_lock lock{m_mutex};
            m_collector.Add(m_root, ChangeCollector::Clock::now());
            return;
        }

        if (IsInsideGitDirectory(relative))
            return;

        // What changed is the entry; what the tree refreshes is the directory holding it.
        std::filesystem::path directory = path.parent_path();
        directory.make_preferred();

        std::scoped_lock lock{m_mutex};
        m_collector.Add(directory, ChangeCollector::Clock::now());
#else
        (void)path;
#endif
    }

    DirectoryWatcher::Changes DirectoryWatcher::TakeSettled()
    {
        // One lock for all three: they describe the same batch, and separate calls would
        // let another take run in between and consume half of it.
        std::scoped_lock lock{m_mutex};

        Changes changes;
        changes.directories = m_collector.TakeSettled(ChangeCollector::Clock::now(), kQuietPeriod);
        changes.overflowed = m_collector.Overflowed();
        changes.movedTo = std::exchange(m_movedTo, std::nullopt);

        return changes;
    }
}
