#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Base/Base.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "Core/Base/Base.h"

#include "Core/Base/Log.h"

#ifdef CORE_PLATFORM_WINDOWS
#include "Core/Base/Base.h"

#include <windows.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr DWORD kBufferBytes = 64 * 1024;

		constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
								  FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

		// Bounds the wait for the arming signal, not Start as a whole. On expiry Start
		// destroys the watcher and the destructor joins the worker with no bound of its own.
		// That is deliberate: detaching a thread that still owns the handle would trade a
		// hang for a use-after-free.
		constexpr std::chrono::seconds kArmTimeout{10};

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
		if (m_StopEvent != nullptr)
			::SetEvent(static_cast<HANDLE>(m_StopEvent));

		if (m_Thread.joinable())
			m_Thread.join();

		if (m_StopEvent != nullptr)
			::CloseHandle(static_cast<HANDLE>(m_StopEvent));

		if (m_Directory != nullptr)
			::CloseHandle(static_cast<HANDLE>(m_Directory));
#endif
	}

	Result<Scope<DirectoryWatcher>> DirectoryWatcher::Start(
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
		Scope<DirectoryWatcher> watcher{new DirectoryWatcher};

		watcher->m_Root = root;
		watcher->m_Directory = directory;
		watcher->m_StopEvent = stopEvent;

		// Taken before the thread starts, so the worker never sees an empty baseline and
		// mistakes it for a move.
		watcher->m_WatchedPath = watcher->QueryHandlePath();
		watcher->m_LastMoveCheck = ChangeCollector::Clock::now();

		// Taken before the thread exists, so there is no race over who reaches the shared
		// state of the promise first.
		std::future<Result<void>> armed = watcher->m_Armed.get_future();

		// Started last: the thread reads the members above, so they must all be set.
		watcher->m_Thread = std::thread{[raw = watcher.get()] { raw->Run(); }};

		// The handle alone records nothing. Windows begins buffering changes for a
		// directory only once a ReadDirectoryChangesW has been issued against it, and that
		// first read happens on the worker — so returning here would hand back a watcher
		// that is blind for as long as it takes the new thread to be scheduled, and
		// anything created in that window would never be reported at all. Wait for the
		// worker to say the watch is armed, or to say it never will be.
		if (armed.wait_for(kArmTimeout) != std::future_status::ready)
		{
			// Returning destroys `watcher`, whose destructor joins the worker — so this
			// branch is not an escape hatch from a stuck thread, and a caller debugging a
			// hung open-folder should look at the join rather than at this timeout. A
			// worker that has simply not been scheduled joins at once; one wedged in a
			// blocking call on a dead network path holds Start there until it unwedges.
			// See kArmTimeout for why that is preferred to detaching.
			return Error{ErrorCode::IoFailure,
						 std::format("timed out arming the watch on '{}'", root.string())};
		}

		if (const Result<void> outcome = armed.get(); !outcome)
			return outcome.GetError();

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
		} runningFlag{m_Running};

		// Start is blocked until this is answered, so every way out of this function
		// before the watch is armed has to answer it — including ones added later.
		// Answering from a destructor is what makes that true by construction rather than
		// by remembering; the paths that know better say so first and this becomes a
		// no-op, since only the first answer counts.
		struct ArmSignal
		{
			std::promise<Result<void>>& promise;

			Result<void> unarmed{Error{ErrorCode::IoFailure,
									   "the watcher thread stopped before the watch was armed"}};

			bool answered = false;

			void Report(Result<void> outcome)
			{
				if (answered)
					return;

				answered = true;
				promise.set_value(std::move(outcome));
			}

			~ArmSignal() { Report(std::move(unarmed)); }
		} armSignal{m_Armed};

#ifdef CORE_PLATFORM_WINDOWS
		// DWORD-aligned as ReadDirectoryChangesW requires; a vector<char> would not be.
		std::vector<DWORD> buffer(kBufferBytes / sizeof(DWORD));

		const HANDLE ioEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

		if (ioEvent == nullptr)
		{
			// Reported to Start rather than only logged: this happens before the watch is
			// armed, so the caller is still waiting and can be told the truth.
			armSignal.Report(
				Error{ErrorCode::IoFailure, "cannot create the watcher I/O event"});

			return;
		}

		while (true)
		{
			OVERLAPPED overlapped{};
			overlapped.hEvent = ioEvent;
			::ResetEvent(ioEvent);

			DWORD ignored = 0;

			if (!::ReadDirectoryChangesW(static_cast<HANDLE>(m_Directory), buffer.data(),
										 kBufferBytes, TRUE, kFilter, &ignored, &overlapped,
										 nullptr))
			{
				// On the first pass Start is still waiting and turns this into an error the
				// caller can act on, so logging here as well would say the same thing
				// twice. Later there is no one left to tell, and the log is all there is.
				if (armSignal.answered)
				{
					CORE_WARN("Stopped watching '{}': the directory is no longer readable",
							  m_Root.string());
				}

				armSignal.Report(Error{
					ErrorCode::IoFailure,
					std::format("cannot watch '{}': the directory is not readable",
								m_Root.string())});

				break;
			}

			// From here the kernel is recording changes for this handle, which is the
			// moment Start is waiting for. Set once — the read above runs on every
			// iteration, but only the first one is the one that opened the window.
			armSignal.Report(Result<void>::Ok());

			const HANDLE waited[] = {ioEvent, static_cast<HANDLE>(m_StopEvent)};
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
							  m_Root.string());
				}

				// Cancel the outstanding read and wait for
				// the kernel to finish with the buffer before it goes out of scope —
				// skipping this is a use-after-free that fires only under load.
				::CancelIoEx(static_cast<HANDLE>(m_Directory), &overlapped);

				DWORD discarded = 0;
				::GetOverlappedResult(static_cast<HANDLE>(m_Directory), &overlapped, &discarded,
									  TRUE);
				break;
			}

			DWORD bytes = 0;

			if (!::GetOverlappedResult(static_cast<HANDLE>(m_Directory), &overlapped, &bytes,
									   FALSE))
			{
				// An overflow is reported here on some paths and as a zero byte count on
				// others, depending on how the status is surfaced. Both are the same
				// event and neither is fatal — treating this one as a failure would end
				// the loop and silently stop watching for the rest of the session,
				// exactly when the disk is busiest.
				if (::GetLastError() == ERROR_NOTIFY_ENUM_DIR)
				{
					std::scoped_lock lock{m_Mutex};
					m_Collector.AddOverflow(ChangeCollector::Clock::now());
					continue;
				}

				// Anything else is fatal. Say so: from here the watcher is permanently
				// dead and the consumer just sees nothing ever change again, which is
				// very hard to guess at.
				CORE_WARN("Stopped watching '{}': the change notification failed",
						  m_Root.string());
				break;
			}

			if (bytes == 0)
			{
				// The other spelling of the same overflow: the kernel could not keep up
				// and discarded the individual records, so the consumer has to assume
				// everything it holds is stale.
				std::scoped_lock lock{m_Mutex};
				m_Collector.AddOverflow(ChangeCollector::Clock::now());
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
							  m_Root.string());
					break;
				}

				const std::wstring name(record->FileName,
										record->FileNameLength / sizeof(wchar_t));

				RecordChange(m_Root / name);

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
			::GetFinalPathNameByHandleW(static_cast<HANDLE>(m_Directory), nullptr, 0,
										FILE_NAME_NORMALIZED);

		if (length == 0)
			return {};

		std::wstring buffer(length, L'\0');

		const DWORD written = ::GetFinalPathNameByHandleW(
			static_cast<HANDLE>(m_Directory), buffer.data(), length, FILE_NAME_NORMALIZED);

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
		if (m_WatchedPath.empty())
		{
			m_WatchedPath = current;
			m_Root = current;
			return;
		}

		if (current == m_WatchedPath)
			return;

		CORE_INFO("The watched directory moved from '{}' to '{}'", m_WatchedPath.string(),
				  current.string());

		// The baseline advances, so the move is reported once rather than on every poll
		// from here on.
		m_WatchedPath = current;

		// So does the root every reported path is built from. Both are touched only by
		// this thread after Start returns, so neither needs the lock.
		m_Root = current;

		std::scoped_lock lock{m_Mutex};

		// Whatever was already collected is spelled under the old location and matches
		// nothing any more. Dropping it here rather than leaving it to the consumer
		// matters: only the batch carrying `movedTo` is documented as suspect, so
		// anything still inside the quiet period would surface in the *next* batch
		// looking perfectly current.
		m_Collector = ChangeCollector{};
		m_MovedTo = current;
#endif
	}

	void DirectoryWatcher::CheckForMoveIfDue()
	{
		const auto now = ChangeCollector::Clock::now();

		if (now - m_LastMoveCheck < kMoveCheckInterval)
			return;

		m_LastMoveCheck = now;
		CheckForMove();
	}

	void DirectoryWatcher::RecordChange(const std::filesystem::path& path)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path relative = path.lexically_relative(m_Root);

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
			std::scoped_lock lock{m_Mutex};
			m_Collector.Add(m_Root, ChangeCollector::Clock::now());
			return;
		}

		if (IsInsideGitDirectory(relative))
			return;

		// What changed is the entry; what the tree refreshes is the directory holding it.
		std::filesystem::path directory = path.parent_path();
		directory.make_preferred();

		std::scoped_lock lock{m_Mutex};
		m_Collector.Add(directory, ChangeCollector::Clock::now());
#else
		(void)path;
#endif
	}

	DirectoryWatcher::Changes DirectoryWatcher::TakeSettled()
	{
		// One lock for all three: they describe the same batch, and separate calls would
		// let another take run in between and consume half of it.
		std::scoped_lock lock{m_Mutex};

		Changes changes;
		changes.directories = m_Collector.TakeSettled(ChangeCollector::Clock::now(), kQuietPeriod);
		changes.overflowed = m_Collector.HasOverflowed();
		changes.movedTo = std::exchange(m_MovedTo, std::nullopt);

		return changes;
	}
}