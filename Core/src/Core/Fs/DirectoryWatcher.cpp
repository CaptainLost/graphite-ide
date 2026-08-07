#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"

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
            const DWORD signalled = ::WaitForMultipleObjects(2, waited, FALSE, INFINITE);

            if (signalled != WAIT_OBJECT_0)
            {
                // Stopping, or the wait failed. Cancel the outstanding read and wait for
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
                break;

            if (bytes == 0)
            {
                // The kernel could not keep up and dropped the individual records. The
                // consumer has to assume everything it holds is stale.
                std::scoped_lock lock{m_mutex};
                m_collector.AddOverflow(ChangeCollector::Clock::now());
                continue;
            }

            const auto* record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data());

            while (true)
            {
                const std::wstring name(record->FileName,
                                        record->FileNameLength / sizeof(wchar_t));

                RecordChange(m_root / name);

                if (record->NextEntryOffset == 0)
                    break;

                record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<const std::byte*>(record) + record->NextEntryOffset);
            }
        }

        ::CloseHandle(ioEvent);
#endif
    }

    void DirectoryWatcher::RecordChange(const std::filesystem::path& path)
    {
#ifdef CORE_PLATFORM_WINDOWS
        const std::filesystem::path relative = path.lexically_relative(m_root);

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

    std::vector<std::filesystem::path> DirectoryWatcher::TakeSettledChanges()
    {
        std::scoped_lock lock{m_mutex};
        return m_collector.TakeSettled(ChangeCollector::Clock::now(), kQuietPeriod);
    }

    bool DirectoryWatcher::Overflowed() const
    {
        // const_cast rather than a mutable member: the mutex protects a logically const
        // read, and this keeps the member declaration honest about what it guards.
        std::scoped_lock lock{const_cast<std::mutex&>(m_mutex)};
        return m_collector.Overflowed();
    }
}
