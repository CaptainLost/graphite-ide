#include "corepch.h"

#include "Core/Platform/PseudoConsole.h"

#include "Core/Platform/CommandLine.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Owners.h"
#include "Core/Platform/Win32Error.h"
#include "Core/Platform/Win32Io.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <atomic>
#include <cstdint>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		// The host keeps writing for a moment after the shell dies, a final newline or a cursor
		// restore, so a read waits this long after the process handle signals before calling it the
		// end of the output.
		constexpr DWORD kFlushGraceMilliseconds = 250;

		// Bounds, not estimates: they keep a wedged shell from blocking the destructor. Running out
		// leaks nothing, since the handles are closed anyway; only the last of the shell's output is
		// dropped.
		constexpr DWORD kShutdownWaitMilliseconds = 2000;
		constexpr DWORD kShutdownDrainMilliseconds = 250;

		constexpr DWORD kNoWaitMilliseconds = 0;

		constexpr DWORD kOnlyPipeInstance = 1;
		constexpr DWORD kPipeBufferBytes = 65536;
		constexpr DWORD kSystemDefaultPipeTimeout = 0;

		constexpr DWORD kDrainChunkBytes = 4096;

		constexpr DWORD kWatchedHandleCount = 2;
		constexpr DWORD kReadEventOnly = 1;
		constexpr DWORD kReadFinished = WAIT_OBJECT_0;
		constexpr DWORD kShellExited = WAIT_OBJECT_0 + 1;

		constexpr DWORD kPseudoConsoleAttributeCount = 1;

		constexpr UINT kTerminatedExitCode = 1;

		HANDLE CreateUnsignalledManualResetEvent()
		{
			const BOOL manualReset = TRUE;
			const BOOL initiallySignalled = FALSE;
			return ::CreateEventW(nullptr, manualReset, initiallySignalled, nullptr);
		}

		COORD ToConsoleSize(std::uint16_t columns, std::uint16_t rows)
		{
			return COORD{.X = static_cast<SHORT>(columns), .Y = static_cast<SHORT>(rows)};
		}

		// CreatePipe cannot make an overlapped end, and the reading end must be one: the host owns
		// the writing end for as long as the pseudo-console lives, so a killed shell never closes it,
		// and Read has to wait on the shell's process handle alongside the pipe. A uniquely named
		// pipe nobody else can reach behaves as an anonymous pipe with that one extra flag.
		bool CreateOverlappedPipe(UniqueHandle& readEnd, UniqueHandle& writeEnd)
		{
			static std::atomic<std::uint64_t> nextPipeNumber{0};

			wchar_t pipeName[MAX_PATH]{};
			::swprintf_s(pipeName, L"\\\\.\\pipe\\Core.PseudoConsole.%lu.%llu", ::GetCurrentProcessId(),
						 static_cast<unsigned long long>(nextPipeNumber.fetch_add(1)));

			readEnd.Reset(::CreateNamedPipeW(
				pipeName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_WAIT, kOnlyPipeInstance, kPipeBufferBytes, kPipeBufferBytes,
				kSystemDefaultPipeTimeout, nullptr));

			if (!readEnd)
			{
				return false;
			}

			const DWORD shareWithNoOne = 0;
			writeEnd.Reset(::CreateFileW(pipeName, GENERIC_WRITE, shareWithNoOne, nullptr, OPEN_EXISTING,
										 FILE_ATTRIBUTE_NORMAL, nullptr));

			if (!writeEnd)
			{
				// Closing a handle overwrites the thread's last error, which the caller reports as the reason the pipe could not be opened.
				const DWORD openErrorCode = ::GetLastError();
				readEnd.Reset();
				::SetLastError(openErrorCode);
				return false;
			}

			return true;
		}

		// The host, not the shell, owns the pipe's writing end, so only the shell's process handle can
		// release a reader once the shell is killed. The flag is passed rather than its value because a
		// reader is usually parked here already when Terminate runs, and must see the flag as it is then.
		std::size_t AwaitRead(HANDLE pipe, OVERLAPPED& overlapped, HANDLE shellProcess,
							  const std::atomic<bool>& terminateRequested)
		{
			const HANDLE watchedHandles[kWatchedHandleCount]{overlapped.hEvent, shellProcess};
			const bool hasShellProcess = shellProcess != nullptr;
			const DWORD watchedHandleCount = hasShellProcess ? kWatchedHandleCount : kReadEventOnly;
			const BOOL waitForAllHandles = FALSE;

			DWORD waitResult = ::WaitForMultipleObjects(watchedHandleCount, watchedHandles, waitForAllHandles, INFINITE);

			if (waitResult == kShellExited)
			{
				// A shell killed on purpose has no last words worth waiting for, and the grace would
				// otherwise be paid on every tab close, on the thread being joined.
				const bool killedOnPurpose = terminateRequested.load(std::memory_order_relaxed);
				const DWORD graceMilliseconds = killedOnPurpose ? kNoWaitMilliseconds : kFlushGraceMilliseconds;

				waitResult = ::WaitForSingleObject(overlapped.hEvent, graceMilliseconds);
			}

			if (waitResult != kReadFinished)
			{
				CancelAndCollect(pipe, overlapped);
				return 0;
			}

			DWORD bytesTransferred = 0;
			const BOOL waitForCompletion = FALSE;
			const BOOL readCollected = ::GetOverlappedResult(pipe, &overlapped, &bytesTransferred, waitForCompletion);

			if (!readCollected)
			{
				return 0;
			}

			return static_cast<std::size_t>(bytesTransferred);
		}

		void DrainPipe(HANDLE pipe, DWORD budgetMilliseconds)
		{
			const UniqueHandle readEventOwner{CreateUnsignalledManualResetEvent()};

			if (!readEventOwner)
			{
				return;
			}

			const HANDLE readEvent = readEventOwner.Get();

			const ULONGLONG deadline = ::GetTickCount64() + budgetMilliseconds;
			char drainBuffer[kDrainChunkBytes]{};

			while (true)
			{
				const ULONGLONG now = ::GetTickCount64();

				if (now >= deadline)
				{
					break;
				}

				// Only what is already buffered is taken: with the shell dead, a read for bytes that
				// never arrive pends until the deadline, and every tab close would stall for the budget.
				DWORD bytesAvailable = 0;
				const DWORD noPeekBuffer = 0;
				const BOOL peeked = ::PeekNamedPipe(pipe, nullptr, noPeekBuffer, nullptr, &bytesAvailable, nullptr);
				const bool nothingBuffered = !peeked || bytesAvailable == 0;

				if (nothingBuffered)
				{
					break;
				}

				::ResetEvent(readEvent);

				OVERLAPPED overlapped{};
				overlapped.hEvent = readEvent;

				DWORD bytesTransferred = 0;
				const BOOL completedAtOnce = ::ReadFile(pipe, drainBuffer, kDrainChunkBytes, &bytesTransferred, &overlapped);

				if (completedAtOnce)
				{
					if (bytesTransferred == 0)
					{
						break;
					}

					continue;
				}

				const bool readPending = ::GetLastError() == ERROR_IO_PENDING;

				if (!readPending)
				{
					break;
				}

				const DWORD timeLeftMilliseconds = static_cast<DWORD>(deadline - now);
				const DWORD waitResult = ::WaitForSingleObject(readEvent, timeLeftMilliseconds);

				if (waitResult != kReadFinished)
				{
					CancelAndCollect(pipe, overlapped);
					break;
				}

				const BOOL waitForCompletion = FALSE;
				const BOOL readCollected = ::GetOverlappedResult(pipe, &overlapped, &bytesTransferred, waitForCompletion);

				if (!readCollected || bytesTransferred == 0)
				{
					break;
				}
			}
		}

		struct ConsolePipes
		{
			UniqueHandle InputRead;
			UniqueHandle InputWrite;
			UniqueHandle OutputRead;
			UniqueHandle OutputWrite;
		};

		Result<ConsolePipes> CreatePipes()
		{
			ConsolePipes pipes;
			HANDLE inputRead = nullptr;
			HANDLE inputWrite = nullptr;

			const DWORD systemDefaultBufferBytes = 0;
			const BOOL inputPipeCreated = ::CreatePipe(&inputRead, &inputWrite, nullptr, systemDefaultBufferBytes);

			if (!inputPipeCreated)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
							 .Message = "cannot create the shell's input pipe"};
			}

			pipes.InputRead.Reset(inputRead);
			pipes.InputWrite.Reset(inputWrite);

			const bool outputPipeCreated = CreateOverlappedPipe(pipes.OutputRead, pipes.OutputWrite);

			if (!outputPipeCreated)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
							 .Message = "cannot create the shell's output pipe"};
			}

			return pipes;
		}

		Result<UniquePseudoConsole> CreateConsole(ConsolePipes& pipes, std::uint16_t columns,
												  std::uint16_t rows)
		{
			const COORD consoleSize = ToConsoleSize(columns, rows);
			UniquePseudoConsole console;
			const DWORD noFlags = 0;
			const HRESULT createResult = ::CreatePseudoConsole(
				consoleSize, pipes.InputRead.Get(), pipes.OutputWrite.Get(), noFlags, console.Receive());

			// ConPTY duplicates the two ends it was handed, so this side's copies are closed as soon as the
			// call returns; held, they would keep the pipes from breaking if the host goes away, and Write
			// would block on a full pipe instead of failing.
			pipes.InputRead.Reset();
			pipes.OutputWrite.Reset();

			if (FAILED(createResult))
			{
				return Error{.Code = ErrorCode::Unsupported, .Message = "this system has no pseudo-console support"};
			}

			return console;
		}

		Result<ProcThreadAttributeList> BuildAttributes(HPCON console)
		{
			ProcThreadAttributeList attributes;

			const bool initialised = attributes.Initialise(kPseudoConsoleAttributeCount);

			if (!initialised)
			{
				return Error{.Code = ErrorCode::IoFailure, .Message = "cannot prepare the process attribute list"};
			}

			const bool consoleAttached = attributes.Update(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console));

			if (!consoleAttached)
			{
				return Error{.Code = ErrorCode::IoFailure,
							 .Message = "cannot attach the pseudo-console to the process attributes"};
			}

			return attributes;
		}

		Result<UniqueHandle> SpawnShell(ProcThreadAttributeList& attributes,
										const std::filesystem::path& program,
										const std::vector<std::string>& arguments,
										const std::filesystem::path& workingDirectory)
		{
			STARTUPINFOEXW startupInfo{};
			startupInfo.StartupInfo.cb = sizeof(startupInfo);
			startupInfo.lpAttributeList = attributes.Get();

			// Naming the standard handles, as null, is what binds the child to the pseudo-console, which
			// replaces them before the child runs. Left unnamed, the child writes its prompt to the console
			// this process is attached to, while every call here still reports success.
			startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.StartupInfo.hStdInput = nullptr;
			startupInfo.StartupInfo.hStdOutput = nullptr;
			startupInfo.StartupInfo.hStdError = nullptr;

			std::wstring commandLine = BuildWindowsCommandLine(program, arguments);
			const std::wstring workingDirectoryText = workingDirectory.wstring();
			const wchar_t* const inheritOurDirectory = nullptr;
			const wchar_t* const childDirectory = workingDirectoryText.empty() ? inheritOurDirectory : workingDirectoryText.c_str();

			PROCESS_INFORMATION processInformation{};
			const BOOL inheritHandles = FALSE;

			// Without EXTENDED_STARTUPINFO_PRESENT, CreateProcessW ignores lpAttributeList and the child
			// starts with no pseudo-console, behaving as though it were on a pipe.
			const BOOL started = ::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, inheritHandles,
												  EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
												  nullptr, childDirectory, &startupInfo.StartupInfo, &processInformation);

			if (!started)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "cannot start the shell"};
			}

			const UniqueHandle unusedThreadClosedOnReturn{processInformation.hThread};
			return UniqueHandle{processInformation.hProcess};
		}
	}
#endif

	Result<Scope<PseudoConsole>> PseudoConsole::Start([[maybe_unused]] const std::filesystem::path& program,
													  [[maybe_unused]] const std::vector<std::string>& arguments,
													  [[maybe_unused]] const std::filesystem::path& workingDirectory,
													  [[maybe_unused]] std::uint16_t columns, [[maybe_unused]] std::uint16_t rows)
	{
#ifdef CORE_PLATFORM_WINDOWS
		Result<ConsolePipes> pipes = CreatePipes();

		if (!pipes)
		{
			return pipes.GetError();
		}

		Result<UniquePseudoConsole> console = CreateConsole(pipes.GetValue(), columns, rows);

		if (!console)
		{
			return console.GetError();
		}

		Result<ProcThreadAttributeList> attributes = BuildAttributes(console.GetValue().Get());

		if (!attributes)
		{
			return attributes.GetError();
		}

		Result<UniqueHandle> process =
			SpawnShell(attributes.GetValue(), program, arguments, workingDirectory);

		if (!process)
		{
			return process.GetError();
		}

		Scope<PseudoConsole> shell{new PseudoConsole};
		shell->m_Console = console.GetValue().Release();
		shell->m_Process = std::move(process.GetValue());
		shell->m_ToChild = std::move(pipes.GetValue().InputWrite);
		shell->m_FromChild = std::move(pipes.GetValue().OutputRead);
		shell->m_Attributes = attributes.GetValue().Release();

		return shell;
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "starting a pseudo-console is only implemented on Windows"};
#endif
	}

	PseudoConsole::~PseudoConsole()
	{
#ifdef CORE_PLATFORM_WINDOWS
		Terminate();

		// TerminateProcess returns before the process is gone, and what follows assumes nothing still
		// feeds the pseudo-console. The wait is bounded so that a shell refusing to die cannot hang the
		// destructor; the handles are closed regardless.
		if (m_Process)
		{
			::WaitForSingleObject(m_Process.Get(), kShutdownWaitMilliseconds);
		}

		// ClosePseudoConsole waits for the host's final flush to land in the pipe, and nobody reads it
		// by then, so a full pipe would leave the close unable to return. Draining it first, within a
		// bound, gives that flush room.
		if (m_FromChild)
		{
			DrainPipe(m_FromChild.Get(), kShutdownDrainMilliseconds);
		}

		// The pseudo-console closes while the reading end is still open: closing it is the host's cue
		// to shut down, and its last flush needs a pipe to write into.
		if (m_Console != nullptr)
		{
			::ClosePseudoConsole(static_cast<HPCON>(m_Console));
			m_Console = nullptr;
		}

		m_FromChild.Reset();
		m_ToChild.Reset();

		if (m_Attributes != nullptr)
		{
			ProcThreadAttributeList::Destroy(
				static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_Attributes));
			m_Attributes = nullptr;
		}

		m_Process.Reset();
#endif
	}

	std::size_t PseudoConsole::Read([[maybe_unused]] std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_FromChild || buffer.empty())
		{
			return 0;
		}

		const HANDLE pipe = m_FromChild.Get();

		const UniqueHandle readEvent{CreateUnsignalledManualResetEvent()};

		if (!readEvent)
		{
			return 0;
		}

		OVERLAPPED overlapped{};
		overlapped.hEvent = readEvent.Get();

		DWORD bytesTransferred = 0;
		std::size_t bytesRead = 0;

		const BOOL completedAtOnce = ::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()),
												&bytesTransferred, &overlapped);

		if (completedAtOnce)
		{
			bytesRead = static_cast<std::size_t>(bytesTransferred);
		}
		else
		{
			const bool readPending = ::GetLastError() == ERROR_IO_PENDING;

			if (readPending)
			{
				bytesRead = AwaitRead(pipe, overlapped, m_Process.Get(), m_TerminateRequested);
			}
		}

		return bytesRead;
#else
		return 0;
#endif
	}

	bool PseudoConsole::Write([[maybe_unused]] std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_ToChild)
		{
			return false;
		}

		return WriteAll(m_ToChild.Get(), bytes);
#else
		return false;
#endif
	}

	Result<void> PseudoConsole::Resize([[maybe_unused]] std::uint16_t columns, [[maybe_unused]] std::uint16_t rows)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Console == nullptr)
		{
			return Error{.Code = ErrorCode::InvalidArgument, .Message = "the pseudo-console is closed"};
		}

		const COORD consoleSize = ToConsoleSize(columns, rows);
		const HRESULT resizeResult = ::ResizePseudoConsole(static_cast<HPCON>(m_Console), consoleSize);

		if (FAILED(resizeResult))
		{
			return Error{.Code = ErrorCode::IoFailure, .Message = "cannot resize the pseudo-console"};
		}

		return Result<void>::Ok();
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "resizing a pseudo-console is only implemented on Windows"};
#endif
	}

	void PseudoConsole::Terminate()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return;
		}

		// Raised before the kill: a reader parked in AwaitRead wakes the moment the shell dies and reads
		// the flag then, so it must already be set or that reader waits out the full flush grace.
		m_TerminateRequested = true;

		::TerminateProcess(m_Process.Get(), kTerminatedExitCode);
#endif
	}

	bool PseudoConsole::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return false;
		}

		return ::WaitForSingleObject(m_Process.Get(), kNoWaitMilliseconds) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
