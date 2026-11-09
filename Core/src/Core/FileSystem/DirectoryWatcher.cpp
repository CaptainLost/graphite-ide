#include "corepch.h"

#include "Core/FileSystem/DirectoryWatcher.h"

#include "Core/Base/Base.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/FileSystem/WslSharePath.h"
#include "Core/Platform/Win32Io.h"

#include <functional>

#ifdef CORE_PLATFORM_WINDOWS

#include <windows.h>
#endif

namespace Core {

	namespace {
		// Reports from its destructor so that every way out of Run before the watch is armed releases Start. Neither copyable nor movable, because a copy would answer the promise a second time, which throws.
		struct ArmSignal
		{
			explicit ArmSignal(std::promise<Result<void>>& armed) : Promise(armed) {}

			ArmSignal(const ArmSignal&) = delete;
			ArmSignal& operator=(const ArmSignal&) = delete;
			ArmSignal(ArmSignal&&) = delete;
			ArmSignal& operator=(ArmSignal&&) = delete;

			std::promise<Result<void>>& Promise;

			Result<void> Unarmed{Error{.Code = ErrorCode::IoFailure, .Message = "the watcher thread stopped before the watch was armed"}};

			bool Answered = false;

			void Report(Result<void> outcome)
			{
				if (Answered)
				{
					return;
				}

				Answered = true;
				Promise.set_value(std::move(outcome));
			}

			void ReportArmed() { Report(Result<void>::Ok()); }

			~ArmSignal() { Report(std::move(Unarmed)); }
		};

		constexpr const char* kIgnoreRulesFileName = ".gitignore";

		bool IsRepositoryStateFile(const std::filesystem::path& fileName)
		{
			return fileName == "HEAD" || fileName == "index" || fileName == "packed-refs";
		}

		bool IsInsideGitDirectory(const std::filesystem::path& pathRelativeToRoot)
		{
			for (const std::filesystem::path& enclosingDirectory : pathRelativeToRoot.parent_path())
			{
				if (enclosingDirectory == ".git")
				{
					return true;
				}
			}

			return false;
		}

		bool IsGitExcludeFile(const std::filesystem::path& pathRelativeToRoot)
		{
			return pathRelativeToRoot == std::filesystem::path{".git"} / "info" / "exclude";
		}

		bool IsGitConfigFile(const std::filesystem::path& pathRelativeToRoot)
		{
			return pathRelativeToRoot == std::filesystem::path{".git"} / "config";
		}

		bool AppliesToTheWholeTree(const std::filesystem::path& pathRelativeToRoot)
		{
			return IsGitExcludeFile(pathRelativeToRoot) || IsGitConfigFile(pathRelativeToRoot);
		}
	}

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr DWORD kNotificationBufferBytes = 64 * 1024;

		constexpr DWORD kWatchedChangeKinds = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
											  FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

		constexpr DWORD kShareModeAllowingRenameAndDelete = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

		// Bounds only the wait for the arming signal. A timed-out Start still joins the worker
		// in the destructor without a bound, because detaching a thread that owns the handle
		// would trade a hang for a use-after-free.
		constexpr std::chrono::seconds kArmTimeout{10};

		constexpr DWORD kWaitedHandleCount = 2;
		constexpr DWORD kIoEventSignalled = WAIT_OBJECT_0;
		constexpr DWORD kStopEventSignalled = WAIT_OBJECT_0 + 1;

		HANDLE CreateUnsignalledManualResetEvent()
		{
			const BOOL manualReset = TRUE;
			const BOOL initiallySignalled = FALSE;
			return ::CreateEventW(nullptr, manualReset, initiallySignalled, nullptr);
		}

		// A network location comes back as \\?\UNC\server\share, where dropping only the first
		// four characters would leave UNC\server\share, which names nothing.
		std::filesystem::path WithoutExtendedPrefix(std::wstring finalPath)
		{
			constexpr std::wstring_view kUncPrefix = LR"(\\?\UNC\)";
			constexpr std::wstring_view kExtendedPrefix = LR"(\\?\)";
			constexpr std::wstring_view kNetworkPathPrefix = LR"(\\)";

			if (finalPath.starts_with(kUncPrefix))
			{
				finalPath.replace(0, kUncPrefix.size(), kNetworkPathPrefix);
			}
			else if (finalPath.starts_with(kExtendedPrefix))
			{
				finalPath.erase(0, kExtendedPrefix.size());
			}

			return std::filesystem::path{finalPath};
		}

		DWORD WaitForChangeOrStop(HANDLE ioEvent, HANDLE stopEvent, DWORD idleIntervalMilliseconds,
								  const std::function<void()>& onWake)
		{
			const HANDLE waitedHandles[kWaitedHandleCount] = {ioEvent, stopEvent};
			const BOOL waitForAllHandles = FALSE;

			while (true)
			{
				const DWORD waitResult =
					::WaitForMultipleObjects(kWaitedHandleCount, waitedHandles, waitForAllHandles, idleIntervalMilliseconds);

				// onWake runs on every wake, not only when the wait times out, because a directory busier than the interval never lets the wait time out. It is skipped once a stop is requested, so that shutdown never waits on a call that a disconnected share can block.
				const bool stopRequested = waitResult == kStopEventSignalled;

				if (!stopRequested)
				{
					onWake();
				}

				const bool timedOut = waitResult == WAIT_TIMEOUT;

				if (!timedOut)
				{
					return waitResult;
				}
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

		// Joined here, before the members close the handles the worker is waiting on.
		if (m_Thread.joinable())
		{
			m_Thread.join();
		}
#endif
	}

	Result<Scope<DirectoryWatcher>> DirectoryWatcher::Start(
		[[maybe_unused]] const std::filesystem::path& root)
	{
		// WARNING: a watch on a WSL share arms without an error and then never reports a change,
		// made from Linux or from Windows (ADR 0688), so it is refused before the disk is touched.
		if (ParseWslSharePath(root).has_value())
		{
			return Error{.Code = ErrorCode::Unsupported,
						 .Message = std::format("changes made inside WSL are not reported to Windows, so '{}' cannot be watched",
												ToUtf8(root))};
		}

#ifdef CORE_PLATFORM_WINDOWS
		UniqueHandle directory{::CreateFileW(
			root.c_str(), FILE_LIST_DIRECTORY, kShareModeAllowingRenameAndDelete, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr)};

		if (!directory)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot watch '{}'", ToUtf8(root))};
		}

		UniqueHandle stopEvent{CreateUnsignalledManualResetEvent()};

		if (!stopEvent)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "cannot create the watcher stop event"};
		}

		// Not make_unique, which cannot reach the private constructor.
		Scope<DirectoryWatcher> watcher{new DirectoryWatcher};

		watcher->m_Root = root;
		watcher->m_Directory = std::move(directory);
		watcher->m_StopEvent = std::move(stopEvent);

		// Everything the worker reads is set here, before the thread that reads it exists. The
		// baseline comes from the handle rather than from `root`, whose case can differ from the
		// case the filesystem stores, which would read as a move on the first check.
		watcher->m_WatchedPath = watcher->QueryHandlePath();
		watcher->m_LastMoveCheck = ChangeCollector::Clock::now();

		std::future<Result<void>> armed = watcher->m_Armed.get_future();

		watcher->m_Thread = std::thread{[watcherPointer = watcher.get()] { watcherPointer->Run(); }};

		const std::future_status armingStatus = armed.wait_for(kArmTimeout);

		if (armingStatus != std::future_status::ready)
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = std::format("timed out arming the watch on '{}'", ToUtf8(root))};
		}

		const Result<void> armingOutcome = armed.get();

		if (!armingOutcome)
		{
			return armingOutcome.GetError();
		}

		return watcher;
#else
		return Error{.Code = ErrorCode::Unsupported, .Message = "watching a directory is not implemented on this platform"};
#endif
	}

	void DirectoryWatcher::Run()
	{
		const ScopeExit clearRunning{[this] { m_Running.store(false); }};

		ArmSignal armSignal{m_Armed};

#ifdef CORE_PLATFORM_WINDOWS
		// DWORD-aligned as ReadDirectoryChangesW requires; a vector<char> would not be.
		std::vector<DWORD> notificationBuffer(kNotificationBufferBytes / sizeof(DWORD));

		const UniqueHandle ownedIoEvent{CreateUnsignalledManualResetEvent()};
		const HANDLE ioEvent = ownedIoEvent.Get();

		if (ioEvent == nullptr)
		{
			armSignal.Report(Error{.Code = ErrorCode::IoFailure, .Message = "cannot create the watcher I/O event"});

			return;
		}

		while (true)
		{
			OVERLAPPED overlapped{};
			overlapped.hEvent = ioEvent;
			::ResetEvent(ioEvent);

			DWORD unusedBytesReturned = 0;
			const BOOL watchSubtree = TRUE;
			const BOOL readIssued = ::ReadDirectoryChangesW(m_Directory.Get(), notificationBuffer.data(),
															kNotificationBufferBytes, watchSubtree, kWatchedChangeKinds,
															&unusedBytesReturned, &overlapped, nullptr);

			if (!readIssued)
			{
				const bool onlyTheLogCanTellAnyone = armSignal.Answered;

				if (onlyTheLogCanTellAnyone)
				{
					CORE_WARN("Stopped watching '{}': the directory is no longer readable",
							  ToUtf8(m_Root));
				}

				armSignal.Report(Error{.Code = ErrorCode::IoFailure, .Message = std::format("cannot watch '{}': the directory is not readable", ToUtf8(m_Root))});

				break;
			}

			armSignal.ReportArmed();

			const DWORD waitResult = WaitForChangeOrStop(
				ioEvent, m_StopEvent.Get(),
				static_cast<DWORD>(kMoveCheckInterval.count()), [this] { CheckForMoveIfDue(); });

			const bool changeArrived = waitResult == kIoEventSignalled;

			if (!changeArrived)
			{
				if (waitResult == WAIT_FAILED)
				{
					CORE_WARN("Stopped watching '{}': the change wait failed",
							  ToUtf8(m_Root));
				}

				// The kernel keeps writing into the buffer until the outstanding read is
				// cancelled and collected; leaving it out is a use-after-free under load.
				CancelAndCollect(m_Directory.Get(), overlapped);
				break;
			}

			DWORD bytesTransferred = 0;
			const BOOL waitForCompletion = FALSE;
			const BOOL readCollected = ::GetOverlappedResult(m_Directory.Get(), &overlapped, &bytesTransferred,
															 waitForCompletion);

			if (!readCollected)
			{
				// An overflow is not fatal: ending the loop here would stop watching for the
				// rest of the session exactly when the disk is busiest.
				const bool overflowReportedAsError = ::GetLastError() == ERROR_NOTIFY_ENUM_DIR;

				if (overflowReportedAsError)
				{
					RecordOverflow();
					Redraw::Request();
					continue;
				}

				CORE_WARN("Stopped watching '{}': the change notification failed",
						  ToUtf8(m_Root));
				break;
			}

			const bool overflowReportedAsEmptyRead = bytesTransferred == 0;

			if (overflowReportedAsEmptyRead)
			{
				RecordOverflow();
				Redraw::Request();
				continue;
			}

			const std::span<const std::byte> wholeBuffer = std::as_bytes(std::span<const DWORD>(notificationBuffer));
			RecordChangesIn(wholeBuffer.subspan(0, bytesTransferred));

			Redraw::Request();
		}
#endif
	}

	void DirectoryWatcher::RecordChangesIn([[maybe_unused]] std::span<const std::byte> filledNotificationRecords)
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Walked by an offset checked against the filled size rather than by following
		// NextEntryOffset on trust, because a malformed chain would otherwise lead past the buffer.
		const std::byte* const firstByte = filledNotificationRecords.data();
		const std::size_t filledBytes = filledNotificationRecords.size();

		for (std::size_t offset = 0; offset < filledBytes;)
		{
			const auto* record = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(firstByte + offset);

			const std::size_t headerBytes = offsetof(FILE_NOTIFY_INFORMATION, FileName);

			// FileNameLength is part of the header, so the header has to be proved to fit before
			// it is read: the left side of the || guards the right.
			if (offset + headerBytes > filledBytes ||
				offset + headerBytes + record->FileNameLength > filledBytes)
			{
				CORE_WARN("Discarding a truncated change record while watching '{}'",
						  ToUtf8(m_Root));
				return;
			}

			const std::wstring fileName(record->FileName,
										record->FileNameLength / sizeof(wchar_t));

			RecordChange(m_Root / fileName);

			const bool isLastRecordInChain = record->NextEntryOffset == 0;

			if (isLastRecordInChain)
			{
				return;
			}

			offset += record->NextEntryOffset;
		}
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
		// GetFinalPathNameByHandleW counts the terminator when the path does not fit and leaves
		// it out when it does.
		const DWORD noBufferCharacters = 0;
		const DWORD sizeWithTerminator =
			::GetFinalPathNameByHandleW(m_Directory.Get(), nullptr, noBufferCharacters,
										FILE_NAME_NORMALIZED);

		if (sizeWithTerminator == 0)
		{
			return {};
		}

		std::wstring finalPath(sizeWithTerminator, L'\0');

		const DWORD lengthWithoutTerminator = ::GetFinalPathNameByHandleW(
			m_Directory.Get(), finalPath.data(), sizeWithTerminator, FILE_NAME_NORMALIZED);

		const bool queryFailed = lengthWithoutTerminator == 0;
		const bool pathGrewBetweenTheTwoQueries = lengthWithoutTerminator >= sizeWithTerminator;

		if (queryFailed || pathGrewBetweenTheTwoQueries)
		{
			return {};
		}

		finalPath.resize(lengthWithoutTerminator);

		return WithoutExtendedPrefix(std::move(finalPath));
#else
		return {};
#endif
	}

	void DirectoryWatcher::CheckForMove()
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path currentPath = QueryHandlePath();

		const bool handleCouldNotBeQueried = currentPath.empty();

		// A handle stops answering while its directory is being deleted, and the failing read then ends the watch, so this is not reported as a move.
		if (handleCouldNotBeQueried)
		{
			return;
		}

		const bool startCouldNotQueryTheBaseline = m_WatchedPath.empty();

		if (startCouldNotQueryTheBaseline)
		{
			m_WatchedPath = currentPath;
			m_Root = currentPath;
			return;
		}

		if (currentPath == m_WatchedPath)
		{
			return;
		}

		CORE_INFO("The watched directory moved from '{}' to '{}'", ToUtf8(m_WatchedPath),
				  ToUtf8(currentPath));

		// Both are touched only by this thread once Start has returned, so neither needs the lock.
		m_WatchedPath = currentPath;
		m_Root = currentPath;

		std::scoped_lock lock{m_Mutex};

		// What was already collected is spelled under the old location, and is dropped here
		// because a change still within its quiet period would otherwise surface in a later
		// batch, one without MovedTo, looking current.
		m_Collector = ChangeCollector{};
		m_MovedTo = currentPath;
		Redraw::Request();
#endif
	}

	void DirectoryWatcher::CheckForMoveIfDue()
	{
		const ChangeCollector::Clock::time_point now = ChangeCollector::Clock::now();

		if (now - m_LastMoveCheck < kMoveCheckInterval)
		{
			return;
		}

		m_LastMoveCheck = now;
		CheckForMove();
	}

	bool NamesRepositoryState(const std::filesystem::path& pathRelativeToRoot)
	{
		std::filesystem::path::iterator component = pathRelativeToRoot.begin();

		if (component == pathRelativeToRoot.end() || *component != ".git")
		{
			return false;
		}

		++component;

		if (component == pathRelativeToRoot.end())
		{
			return false;
		}

		const bool isUnderRefs = *component == "refs";

		if (isUnderRefs)
		{
			return true;
		}

		const bool isDirectlyInsideGitDirectory = std::next(component) == pathRelativeToRoot.end();

		if (!isDirectlyInsideGitDirectory)
		{
			return false;
		}

		return IsRepositoryStateFile(*component);
	}

	bool NamesIgnoreRules(const std::filesystem::path& pathRelativeToRoot)
	{
		const bool isAFolderRulesFile =
			pathRelativeToRoot.filename() == kIgnoreRulesFileName && !IsInsideGitDirectory(pathRelativeToRoot);

		return isAFolderRulesFile || IsGitExcludeFile(pathRelativeToRoot) || IsGitConfigFile(pathRelativeToRoot);
	}

	void DirectoryWatcher::RecordChange([[maybe_unused]] const std::filesystem::path& changedPath)
	{
#ifdef CORE_PLATFORM_WINDOWS
		const std::filesystem::path pathRelativeToRoot = changedPath.lexically_relative(m_Root);

		if (NamesIgnoreRules(pathRelativeToRoot))
		{
			std::scoped_lock lock{m_Mutex};
			m_Collector.AddIgnoreRulesChange(ChangeCollector::Clock::now());
		}

		if (AppliesToTheWholeTree(pathRelativeToRoot))
		{
			std::scoped_lock lock{m_Mutex};
			m_Collector.Add(m_Root, ChangeCollector::Clock::now());
			return;
		}

		if (NamesRepositoryState(pathRelativeToRoot))
		{
			std::scoped_lock lock{m_Mutex};
			m_RepositoryStateChanged = true;
			return;
		}

		if (IsInsideGitDirectory(pathRelativeToRoot))
		{
			return;
		}

		std::filesystem::path directoryHoldingTheEntry = changedPath.parent_path();
		directoryHoldingTheEntry.make_preferred();

		std::scoped_lock lock{m_Mutex};
		m_Collector.Add(directoryHoldingTheEntry, ChangeCollector::Clock::now());
#endif
	}

	DirectoryWatcher::Changes DirectoryWatcher::TakeSettled()
	{
		std::scoped_lock lock{m_Mutex};

		std::vector<std::filesystem::path> settledDirectories = m_Collector.TakeSettled(ChangeCollector::Clock::now(), kQuietPeriod);

		// HasOverflowed describes the batch the most recent TakeSettled handed over, so it is read after that call.
		Changes changes{
			.Directories = std::move(settledDirectories),
			.Overflowed = m_Collector.HasOverflowed(),
			.IgnoreRulesChanged = m_Collector.HasIgnoreRulesChanged(),
			.RepositoryStateChanged = std::exchange(m_RepositoryStateChanged, false),
			.MovedTo = std::exchange(m_MovedTo, std::nullopt),
		};

		// The changes woke the loop for a few frames, not for the whole quiet period, so the
		// frame that will find this batch settled has to be asked for.
		const std::optional<ChangeCollector::Clock::time_point> pendingBatchSettlesAt = m_Collector.SettlesAt(kQuietPeriod);

		if (pendingBatchSettlesAt)
		{
			Redraw::RequestIn(*pendingBatchSettlesAt - ChangeCollector::Clock::now());
		}

		return changes;
	}
}
