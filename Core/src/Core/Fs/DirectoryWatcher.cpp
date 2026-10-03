#include "Core/Base/Base.h"

#include "corepch.h"

#include "Core/Fs/DirectoryWatcher.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Base/ScopeExit.h"
#include "Core/Fs/FileSystem.h"

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr DWORD kBufferBytes = 64 * 1024;

		constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
								  FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

		// Bounds the wait for the arming signal only; on expiry the destructor still joins
		// the worker unbounded, because detaching a thread that owns the handle would trade
		// a hang for a use-after-free.
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

		bool IsGitExcludeFile(const std::filesystem::path& relative)
		{
			return relative == std::filesystem::path{".git"} / "info" / "exclude";
		}

		std::filesystem::path WithoutExtendedPrefix(std::wstring buffer)
		{
			// A network location comes back as \\?\UNC\server\share, where dropping only the
			// first four characters would leave `UNC\server\share`, naming nothing.
			constexpr std::wstring_view kUncPrefix = LR"(\\?\UNC\)";
			constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";

			if (buffer.starts_with(kUncPrefix))
				buffer.replace(0, kUncPrefix.size(), LR"(\\)");
			else if (buffer.starts_with(kExtendedPrefix))
				buffer.erase(0, kExtendedPrefix.size());

			return std::filesystem::path{buffer};
		}

		// Returns only once one of the handles is signalled; the timeout is idle time this
		// borrows while the read stays outstanding.
		template<typename OnIdle>
		DWORD WaitForChangeOrStop(HANDLE ioEvent, HANDLE stopEvent, DWORD idleMilliseconds,
								  OnIdle&& onIdle)
		{
			const HANDLE waited[] = {ioEvent, stopEvent};

			while (true)
			{
				const DWORD signalled =
					::WaitForMultipleObjects(2, waited, FALSE, idleMilliseconds);

				// Driven by elapsed time rather than by the wait expiring: a directory busier
				// than the interval never lets it expire, which is precisely when a move goes
				// unnoticed. Skipped while stopping, so shutdown never waits on an API call a
				// disconnected share could sit on.
				if (signalled != WAIT_OBJECT_0 + 1)
					onIdle();

				if (signalled != WAIT_TIMEOUT)
					return signalled;
			}
		}
	}
#endif

	DirectoryWatcher::~DirectoryWatcher()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_StopEvent)
		{
			::SetEvent(m_StopEvent.Get());
		}

		// Before the handles close as the members are destroyed: the worker waits on both.
		if (m_Thread.joinable())
		{
			m_Thread.join();
		}
#endif
	}

	Result<Scope<DirectoryWatcher>> DirectoryWatcher::Start(
		const std::filesystem::path& root)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// FILE_SHARE_DELETE included on purpose: without it, watching a folder stops anyone
		// from renaming or deleting it while the IDE is open.
		UniqueHandle directory{::CreateFileW(
			root.c_str(), FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr)};

		if (!directory)
		{
			return Error{ErrorCode::IoFailure,
						 std::format("cannot watch '{}'", ToUtf8(root))};
		}

		UniqueHandle stopEvent{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};

		if (!stopEvent)
		{
			return Error{ErrorCode::IoFailure, "cannot create the watcher stop event"};
		}

		// Not make_unique: the constructor is private, and this is the only creator.
		Scope<DirectoryWatcher> watcher{new DirectoryWatcher};

		watcher->m_Root = root;
		watcher->m_Directory = std::move(directory);
		watcher->m_StopEvent = std::move(stopEvent);

		// Everything the worker reads is set here, before the thread that reads it exists.
		watcher->m_WatchedPath = watcher->QueryHandlePath();
		watcher->m_LastMoveCheck = ChangeCollector::Clock::now();

		std::future<Result<void>> armed = watcher->m_Armed.get_future();

		watcher->m_Thread = std::thread{[raw = watcher.get()] { raw->Run(); }};

		if (armed.wait_for(kArmTimeout) != std::future_status::ready)
		{
			// Not an escape hatch from a stuck worker: returning destroys `watcher`, whose
			// destructor joins it. A worker merely not yet scheduled joins at once.
			return Error{ErrorCode::IoFailure,
						 std::format("timed out arming the watch on '{}'", ToUtf8(root))};
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
		const ScopeExit clearRunning{[this] { m_Running.store(false); }};

		// Answered from a destructor so that every way out of this function before the watch
		// is armed releases Start, including ones added later; only the first answer counts.
		// Neither copyable nor movable: a copy would answer the promise twice, which throws.
		struct ArmSignal
		{
			explicit ArmSignal(std::promise<Result<void>>& armed) : promise(armed) {}

			ArmSignal(const ArmSignal&) = delete;
			ArmSignal& operator=(const ArmSignal&) = delete;
			ArmSignal(ArmSignal&&) = delete;
			ArmSignal& operator=(ArmSignal&&) = delete;

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
		};

		ArmSignal armSignal{m_Armed};

#ifdef CORE_PLATFORM_WINDOWS
		// DWORD-aligned as ReadDirectoryChangesW requires; a vector<char> would not be.
		std::vector<DWORD> buffer(kBufferBytes / sizeof(DWORD));

		const UniqueHandle ownedIoEvent{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
		const HANDLE ioEvent = ownedIoEvent.Get();

		if (ioEvent == nullptr)
		{
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

			if (!::ReadDirectoryChangesW(m_Directory.Get(), buffer.data(),
										 kBufferBytes, TRUE, kFilter, &ignored, &overlapped,
										 nullptr))
			{
				// Before the watch is armed, Start is still waiting and turns this into an
				// error the caller can act on; afterwards the log is all there is.
				if (armSignal.answered)
				{
					CORE_WARN("Stopped watching '{}': the directory is no longer readable",
							  ToUtf8(m_Root));
				}

				armSignal.Report(Error{
					ErrorCode::IoFailure,
					std::format("cannot watch '{}': the directory is not readable",
								ToUtf8(m_Root))});

				break;
			}

			// The kernel begins recording changes for this handle only once a read has been
			// issued against it, which is the moment Start is waiting for.
			armSignal.Report(Result<void>::Ok());

			const DWORD signalled = WaitForChangeOrStop(
				ioEvent, m_StopEvent.Get(),
				static_cast<DWORD>(kMoveCheckInterval.count()), [this] { CheckForMoveIfDue(); });

			if (signalled != WAIT_OBJECT_0)
			{
				if (signalled == WAIT_FAILED)
				{
					CORE_WARN("Stopped watching '{}': the change wait failed",
							  ToUtf8(m_Root));
				}

				// The kernel keeps writing into `buffer` until the outstanding read is
				// cancelled and collected; leaving it out is a use-after-free under load.
				::CancelIoEx(m_Directory.Get(), &overlapped);

				DWORD discarded = 0;
				::GetOverlappedResult(m_Directory.Get(), &overlapped, &discarded,
									  TRUE);
				break;
			}

			DWORD bytes = 0;

			if (!::GetOverlappedResult(m_Directory.Get(), &overlapped, &bytes,
									   FALSE))
			{
				// The kernel discarded the individual records because it could not keep up.
				// Not fatal: ending the loop here would stop watching for the rest of the
				// session exactly when the disk is busiest.
				if (::GetLastError() == ERROR_NOTIFY_ENUM_DIR)
				{
					RecordOverflow();
					Redraw::Request();
					continue;
				}

				CORE_WARN("Stopped watching '{}': the change notification failed",
						  ToUtf8(m_Root));
				break;
			}

			// The other spelling of the same overflow.
			if (bytes == 0)
			{
				RecordOverflow();
				Redraw::Request();
				continue;
			}

			RecordChangesIn(std::as_bytes(std::span(buffer)).first(bytes));

			// Nothing is settled yet — that takes a quiet period — but a take in the frames
			// this buys learns when it will be, and asks for a frame then.
			Redraw::Request();
		}
#endif
	}

	void DirectoryWatcher::RecordChangesIn(std::span<const std::byte> records)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Walked with an explicit offset checked against `bytes` rather than by following
		// NextEntryOffset on trust: a malformed chain would otherwise read past the buffer.
		const std::byte* const base = records.data();
		const std::size_t bytes = records.size();

		for (std::size_t offset = 0; offset < bytes;)
		{
			const auto* record =
				reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset);

			const std::size_t headerBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);

			// The header has to be proved to fit before FileNameLength is read, because that
			// field is part of the header: testing both at once reads it out of whatever
			// follows the reported data when fewer bytes than a header are left.
			if (offset + headerBytes > bytes ||
				offset + headerBytes + record->FileNameLength > bytes)
			{
				CORE_WARN("Discarding a truncated change record while watching '{}'",
						  ToUtf8(m_Root));
				return;
			}

			const std::wstring name(record->FileName,
									record->FileNameLength / sizeof(wchar_t));

			RecordChange(m_Root / name);

			// Zero terminates the chain, and anything that does not move forward would spin
			// here forever.
			if (record->NextEntryOffset == 0)
				return;

			offset += record->NextEntryOffset;
		}
#else
		(void)records;
#endif
	}

	void DirectoryWatcher::RecordOverflow()
	{
		std::scoped_lock lock{m_Mutex};
		m_Collector.AddOverflow(ChangeCollector::Clock::now());
	}

	std::filesystem::path DirectoryWatcher::QueryHandlePath() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Asked for twice, and the two calls do not agree on what they count: a call that
		// could not fit the path returns the size *including* the terminator, one that
		// succeeded returns the length *excluding* it. So `length` already has room for the
		// terminator, and `written >= length` means the path grew between the two calls.
		const DWORD length =
			::GetFinalPathNameByHandleW(m_Directory.Get(), nullptr, 0,
										FILE_NAME_NORMALIZED);

		if (length == 0)
			return {};

		std::wstring buffer(length, L'\0');

		const DWORD written = ::GetFinalPathNameByHandleW(
			m_Directory.Get(), buffer.data(), length, FILE_NAME_NORMALIZED);

		if (written == 0 || written >= length)
			return {};

		buffer.resize(written);

		return WithoutExtendedPrefix(std::move(buffer));
#else
		return {};
#endif
	}

	void DirectoryWatcher::CheckForMove()
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path current = QueryHandlePath();

		// An unanswerable handle is not a move; it happens while the directory is being
		// deleted, which the failing read ends the loop over anyway.
		if (current.empty())
			return;

		// An empty baseline means the query failed back in Start, not that the directory was
		// somewhere else, so adopt the first answer instead of announcing a move to where we
		// already are.
		if (m_WatchedPath.empty())
		{
			m_WatchedPath = current;
			m_Root = current;
			return;
		}

		if (current == m_WatchedPath)
			return;

		CORE_INFO("The watched directory moved from '{}' to '{}'", ToUtf8(m_WatchedPath),
				  ToUtf8(current));

		// Both are touched only by this thread after Start returns, so neither needs the
		// lock. Advancing the baseline is what reports the move once rather than every poll.
		m_WatchedPath = current;
		m_Root = current;

		std::scoped_lock lock{m_Mutex};

		// Whatever was already collected is spelled under the old location. Dropping it here
		// rather than leaving it to the consumer matters: only the batch carrying `MovedTo`
		// is documented as suspect, so anything still within the quiet period would surface
		// in the next batch looking perfectly current.
		m_Collector = ChangeCollector{};
		m_MovedTo = current;
		Redraw::Request();
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

	bool NamesRepositoryState(const std::filesystem::path& relative)
	{
		auto component = relative.begin();

		if (component == relative.end() || *component != ".git")
		{
			return false;
		}

		++component;

		if (component == relative.end())
		{
			return false;
		}

		// Everything under refs: a branch, a tag or a remote-tracking branch moving is the
		// commonest way history changes under an application that is already open.
		if (*component == "refs")
		{
			return true;
		}

		// The rest are single files directly inside .git, so a deeper path of the same name
		// - logs/HEAD, which git appends to on every operation - is not one of them.
		if (std::next(component) != relative.end())
		{
			return false;
		}

		return *component == "HEAD" || *component == "index" || *component == "packed-refs";
	}

	void DirectoryWatcher::RecordChange(const std::filesystem::path& path)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path relative = path.lexically_relative(m_Root);

		// git's private, uncommitted ignore rules: the one thing under .git worth hearing
		// about, and what changed is the verdict for every entry, so the root is what gets
		// refreshed. Only reachable when the watched directory is the repository's own root.
		if (IsGitExcludeFile(relative))
		{
			std::scoped_lock lock{m_Mutex};
			m_Collector.Add(m_Root, ChangeCollector::Clock::now());
			return;
		}

		// Checked before the blanket .git filter below, which would otherwise drop it: this
		// is the one class of change under .git that says the answers on screen are stale.
		// Recorded as a flag rather than as a directory, because no directory of the working
		// tree changed and refreshing one would be work with no reason behind it.
		if (NamesRepositoryState(relative))
		{
			std::scoped_lock lock{m_Mutex};
			m_RepositoryState = true;
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
		// One lock for all three: they describe the same batch, and separate calls would let
		// another take run in between and consume half of it.
		std::scoped_lock lock{m_Mutex};

		Changes changes;
		changes.Directories = m_Collector.TakeSettled(ChangeCollector::Clock::now(), kQuietPeriod);
		changes.Overflowed = m_Collector.HasOverflowed();
		changes.RepositoryState = std::exchange(m_RepositoryState, false);
		changes.MovedTo = std::exchange(m_MovedTo, std::nullopt);

		// The change woke the loop for a few frames, not for the quiet period, so the frame
		// that finds the batch settled has to be asked for.
		if (const auto settlesAt = m_Collector.SettlesAt(kQuietPeriod))
			Redraw::RequestIn(*settlesAt - ChangeCollector::Clock::now());

		return changes;
	}
}
