#include "corepch.h"

#include "Core/Platform/PseudoConsole.h"

#include "Core/Platform/CommandLine.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Owners.h"
#include "Core/Platform/Win32Error.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>

#include <atomic>
#include <cstdint>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		// How long a read waits for the pseudo-console's last buffered bytes once the shell's
		// process handle has already signalled. The host keeps writing for a moment after the
		// process it hosts dies — a final newline, a cursor restore — and treating the process
		// handle alone as end-of-stream would cut that off.
		constexpr DWORD FlushGraceMilliseconds = 250;

		// The two bounds the destructor runs under. Neither is a guess about how long a shell
		// needs; both exist so that no shell, however wedged, can stop this object from being
		// destroyed. Exceeding either leaks nothing this process owns — the handles are closed
		// regardless — it only means the last of a dying shell's output is dropped.
		constexpr DWORD ShutdownWaitMilliseconds = 2000;
		constexpr DWORD ShutdownDrainMilliseconds = 250;

		// CreatePipe cannot make either end of an anonymous pipe overlapped, and the reading end
		// has to be: a synchronous ReadFile can only be woken by data arriving on that one
		// handle, but the pseudo-console's host owns the writing end for as long as the
		// pseudo-console itself lives, so a killed shell never makes that handle close. Read has
		// to wait on the shell's process handle at the same time as the pipe, and that requires
		// FILE_FLAG_OVERLAPPED. A uniquely named pipe nobody else can reach gets the same
		// anonymous-pipe behaviour with that one extra flag.
		bool CreateOverlappedPipe(UniqueHandle& readEnd, UniqueHandle& writeEnd)
		{
			static std::atomic<std::uint64_t> nextPipe{0};

			wchar_t name[MAX_PATH]{};
			::swprintf_s(name, L"\\\\.\\pipe\\Core.PseudoConsole.%lu.%llu", ::GetCurrentProcessId(),
						 static_cast<unsigned long long>(nextPipe.fetch_add(1)));

			readEnd.Reset(::CreateNamedPipeW(
				name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr));

			if (!readEnd)
			{
				return false;
			}

			writeEnd.Reset(::CreateFileW(name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
										 FILE_ATTRIBUTE_NORMAL, nullptr));

			if (!writeEnd)
			{
				// Closing overwrites the thread's last error, so the caller would report
				// whatever the close did rather than why the pipe could not be opened.
				const DWORD failure = ::GetLastError();
				readEnd.Reset();
				::SetLastError(failure);
				return false;
			}

			return true;
		}

		// Waits for a read already in flight, watching the shell's process handle alongside it.
		// The pipe alone never reports the shell's death — the pseudo-console's host, not the
		// shell, owns the writing end — so the process handle is what lets a killed shell
		// release a blocked reader; without this, Terminate() would kill the shell and a thread
		// in Read would still be waiting on a pipe nothing is ever going to close.
		// The flag is passed rather than its value: a reader is normally already parked here
		// when the tab is closed, so a grace decided at the start of the read would be the one
		// from before the shell was killed. It is read at the moment it is used.
		std::size_t AwaitRead(HANDLE pipe, OVERLAPPED& overlapped, void* process,
							  const std::atomic<bool>& terminating)
		{
			const HANDLE waited[2]{overlapped.hEvent, static_cast<HANDLE>(process)};
			const DWORD count = process != nullptr ? 2u : 1u;

			DWORD signalled = ::WaitForMultipleObjects(count, waited, FALSE, INFINITE);

			if (signalled == WAIT_OBJECT_0 + 1)
			{
				// The process just died. Give the host a short window to deliver whatever it
				// had already flushed before treating this as end-of-stream.
				const DWORD grace = terminating.load(std::memory_order_relaxed)
										? 0u
										: FlushGraceMilliseconds;

				signalled = ::WaitForSingleObject(overlapped.hEvent, grace);
			}

			if (signalled != WAIT_OBJECT_0)
			{
				DWORD discarded = 0;
				::CancelIoEx(pipe, &overlapped);
				::GetOverlappedResult(pipe, &overlapped, &discarded, TRUE);
				return 0;
			}

			DWORD transferred = 0;

			if (!::GetOverlappedResult(pipe, &overlapped, &transferred, FALSE))
			{
				return 0;
			}

			return static_cast<std::size_t>(transferred);
		}

		// Reads and discards whatever is already sitting in the pipe, for at most the given
		// number of milliseconds. Used on the way out, where the point is not the bytes but the
		// room they free: ClosePseudoConsole waits for the host to flush, and at that moment no
		// reader is left, so a full buffer would leave the host unable to finish. The deadline is
		// re-checked before every wait and after every completed read, so a host that keeps
		// producing cannot hold this open.
		void DrainPipe(HANDLE pipe, DWORD milliseconds)
		{
			const UniqueHandle eventHandle{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};

			if (!eventHandle)
			{
				return;
			}

			const HANDLE event = eventHandle.Get();

			const ULONGLONG deadline = ::GetTickCount64() + milliseconds;
			char scratch[4096]{};

			while (true)
			{
				const ULONGLONG now = ::GetTickCount64();

				if (now >= deadline)
				{
					break;
				}

				// Only what is already buffered is taken. Issuing a read for bytes that may
				// never arrive is what made this cost its whole budget every time: with the
				// shell dead the read simply pends until the deadline, and the destructor
				// stalls for a quarter of a second per session on the thread closing the tab.
				DWORD available = 0;

				if (!::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
				{
					break;
				}

				::ResetEvent(event);

				OVERLAPPED overlapped{};
				overlapped.hEvent = event;

				DWORD transferred = 0;

				if (::ReadFile(pipe, scratch, static_cast<DWORD>(sizeof(scratch)), &transferred,
							   &overlapped))
				{
					if (transferred == 0)
					{
						break;
					}

					continue;
				}

				if (::GetLastError() != ERROR_IO_PENDING)
				{
					break;
				}

				if (::WaitForSingleObject(event, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
				{
					::CancelIoEx(pipe, &overlapped);
					::GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
					break;
				}

				if (!::GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) || transferred == 0)
				{
					break;
				}
			}
		}

		// The four ends of the two pipes the pseudo-console is built from.
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

			if (!::CreatePipe(&inputRead, &inputWrite, nullptr, 0))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot create the shell's input pipe"};
			}

			pipes.InputRead.Reset(inputRead);
			pipes.InputWrite.Reset(inputWrite);

			if (!CreateOverlappedPipe(pipes.OutputRead, pipes.OutputWrite))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot create the shell's output pipe"};
			}

			return pipes;
		}

		Result<UniquePseudoConsole> CreateConsole(ConsolePipes& pipes, std::uint16_t columns,
												  std::uint16_t rows)
		{
			const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
			UniquePseudoConsole console;
			const HRESULT created = ::CreatePseudoConsole(
				size, pipes.InputRead.Get(), pipes.OutputWrite.Get(), 0, console.Receive());

			// ConPTY duplicates the two ends it was handed, so this side is done with them the
			// moment the call returns. Holding them open would keep the output pipe's writing end
			// alive after the shell exits, and a reader would then block for ever instead of
			// learning that the shell is gone.
			pipes.InputRead.Reset();
			pipes.OutputWrite.Reset();

			if (FAILED(created))
			{
				return Error{ErrorCode::Unsupported, "this system has no pseudo-console support"};
			}

			return console;
		}

		Result<ProcThreadAttributeList> BuildAttributes(HPCON console)
		{
			ProcThreadAttributeList attributes;

			if (!attributes.Initialise(1))
			{
				return Error{ErrorCode::IoFailure, "cannot prepare the process attribute list"};
			}

			if (!attributes.Update(PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console, sizeof(console)))
			{
				return Error{ErrorCode::IoFailure,
							 "cannot attach the pseudo-console to the process attributes"};
			}

			return attributes;
		}

		// Starts the shell bound to the pseudo-console named by the attribute list and returns
		// its process handle.
		Result<UniqueHandle> SpawnShell(ProcThreadAttributeList& attributes,
										const std::filesystem::path& program,
										const std::vector<std::string>& arguments,
										const std::filesystem::path& workingDirectory)
		{
			STARTUPINFOEXW startup{};
			startup.StartupInfo.cb = sizeof(startup);
			startup.lpAttributeList = attributes.Get();

			// Naming the standard handles — as nothing — is what actually binds the child to the
			// pseudo-console. Left unsaid, the child takes over the console this process is already
			// attached to and writes its prompt there instead, which looks from here exactly like a
			// pseudo-console that never received any output: every call above still reports success.
			// The three null handles are replaced by the pseudo-console's own before the child runs.
			startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			startup.StartupInfo.hStdInput = nullptr;
			startup.StartupInfo.hStdOutput = nullptr;
			startup.StartupInfo.hStdError = nullptr;

			std::wstring commandLine = BuildWindowsCommandLine(program, arguments);
			const std::wstring directory = workingDirectory.wstring();

			PROCESS_INFORMATION information{};

			// EXTENDED_STARTUPINFO_PRESENT is what makes CreateProcessW read lpAttributeList at
			// all; without it the child starts with no pseudo-console and behaves as though it
			// were on a pipe, which is the failure this whole class exists to avoid.
			if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
								  EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
								  nullptr, directory.empty() ? nullptr : directory.c_str(),
								  &startup.StartupInfo, &information))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()), "cannot start the shell"};
			}

			const UniqueHandle thread{information.hThread};
			return UniqueHandle{information.hProcess};
		}
	}
#endif

	Result<Scope<PseudoConsole>> PseudoConsole::Start(const std::filesystem::path& program,
													  const std::vector<std::string>& arguments,
													  const std::filesystem::path& workingDirectory,
													  std::uint16_t columns, std::uint16_t rows)
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

		// Only now is nothing left that can fail, so ownership moves into the object in one go.
		Scope<PseudoConsole> shell{new PseudoConsole};
		shell->m_Console = console.GetValue().Release();
		shell->m_Process = std::move(process.GetValue());
		shell->m_ToChild = std::move(pipes.GetValue().InputWrite);
		shell->m_FromChild = std::move(pipes.GetValue().OutputRead);
		shell->m_Attributes = attributes.GetValue().Release();

		return shell;
#else
		(void)program;
		(void)arguments;
		(void)workingDirectory;
		(void)columns;
		(void)rows;
		return Error{ErrorCode::Unsupported,
					 "starting a pseudo-console is only implemented on Windows"};
#endif
	}

	PseudoConsole::~PseudoConsole()
	{
#ifdef CORE_PLATFORM_WINDOWS
		Terminate();

		// TerminateProcess only asks; it returns long before the process is gone. Everything
		// below is written on the assumption that nothing is still feeding the pseudo-console,
		// so the shell's death is waited for rather than assumed. The wait is bounded because a
		// destructor that can block for ever is worse than one that gives up on a shell which
		// refuses to die: on expiry the handles are closed anyway.
		if (m_Process)
		{
			::WaitForSingleObject(m_Process.Get(), ShutdownWaitMilliseconds);
		}

		// ClosePseudoConsole below waits for the host's final flush to land in the pipe, and by
		// then nobody is draining it — a full buffer would leave the host unable to finish and
		// the close unable to return. Emptying the pipe first hands that flush the whole buffer.
		// Bounded, for the same reason as the wait above.
		if (m_FromChild)
		{
			DrainPipe(m_FromChild.Get(), ShutdownDrainMilliseconds);
		}

		// The pseudo-console goes first, while the reading end is still open: closing it is the
		// host's cue to shut down, and the flush it does on the way out needs a pipe left to
		// write into.
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

	std::size_t PseudoConsole::Read(std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_FromChild || buffer.empty())
		{
			return 0;
		}

		const HANDLE pipe = m_FromChild.Get();

		const UniqueHandle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};

		if (!event)
		{
			return 0;
		}

		OVERLAPPED overlapped{};
		overlapped.hEvent = event.Get();

		DWORD transferred = 0;
		std::size_t read = 0;

		if (::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &transferred,
					   &overlapped))
		{
			read = static_cast<std::size_t>(transferred);
		}
		else if (::GetLastError() == ERROR_IO_PENDING)
		{
			read = AwaitRead(pipe, overlapped, m_Process.Get(), m_Terminating);
		}

		return read;
#else
		(void)buffer;
		return 0;
#endif
	}

	bool PseudoConsole::Write(std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_ToChild)
		{
			return false;
		}

		std::size_t written = 0;

		while (written < bytes.size())
		{
			DWORD chunk = 0;

			if (!::WriteFile(m_ToChild.Get(), bytes.data() + written,
							 static_cast<DWORD>(bytes.size() - written), &chunk, nullptr) ||
				chunk == 0)
			{
				return false;
			}

			written += chunk;
		}

		return true;
#else
		(void)bytes;
		return false;
#endif
	}

	Result<void> PseudoConsole::Resize(std::uint16_t columns, std::uint16_t rows)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Console == nullptr)
		{
			return Error{ErrorCode::InvalidArgument, "the pseudo-console is closed"};
		}

		const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};

		if (FAILED(::ResizePseudoConsole(static_cast<HPCON>(m_Console), size)))
		{
			return Error{ErrorCode::IoFailure, "cannot resize the pseudo-console"};
		}

		return Result<void>::Ok();
#else
		(void)columns;
		(void)rows;
		return Error{ErrorCode::Unsupported,
					 "resizing a pseudo-console is only implemented on Windows"};
#endif
	}

	void PseudoConsole::Terminate()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return;
		}

		// Before the kill, so a read already parked in its grace window is not waiting
		// on a shell that is about to be gone.
		m_Terminating = true;

		::TerminateProcess(m_Process.Get(), 1);
#endif
	}

	bool PseudoConsole::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return false;
		}

		return ::WaitForSingleObject(m_Process.Get(), 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
