#include "corepch.h"

#include "Core/Platform/PseudoConsole.h"

#include "Core/Platform/CommandLine.h"

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

		ErrorCode ErrorCodeFor(DWORD error)
		{
			switch (error)
			{
			case ERROR_FILE_NOT_FOUND:
			case ERROR_PATH_NOT_FOUND:
				return ErrorCode::NotFound;
			default:
				return ErrorCode::IoFailure;
			}
		}

		void CloseIfOpen(void*& handle)
		{
			if (handle != nullptr)
			{
				::CloseHandle(static_cast<HANDLE>(handle));
				handle = nullptr;
			}
		}

		// CreatePipe cannot make either end of an anonymous pipe overlapped, and the reading end
		// has to be: a synchronous ReadFile can only be woken by data arriving on that one
		// handle, but the pseudo-console's host owns the writing end for as long as the
		// pseudo-console itself lives, so a killed shell never makes that handle close. Read has
		// to wait on the shell's process handle at the same time as the pipe, and that requires
		// FILE_FLAG_OVERLAPPED. A uniquely named pipe nobody else can reach gets the same
		// anonymous-pipe behaviour with that one extra flag.
		bool CreateOverlappedPipe(HANDLE& readEnd, HANDLE& writeEnd)
		{
			static std::atomic<std::uint64_t> nextPipe{0};

			wchar_t name[MAX_PATH]{};
			::swprintf_s(name, L"\\\\.\\pipe\\Core.PseudoConsole.%lu.%llu", ::GetCurrentProcessId(),
						 static_cast<unsigned long long>(nextPipe.fetch_add(1)));

			readEnd = ::CreateNamedPipeW(
				name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
				PIPE_TYPE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);

			if (readEnd == INVALID_HANDLE_VALUE)
			{
				readEnd = nullptr;
				return false;
			}

			writeEnd = ::CreateFileW(name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
									 FILE_ATTRIBUTE_NORMAL, nullptr);

			if (writeEnd == INVALID_HANDLE_VALUE)
			{
				::CloseHandle(readEnd);
				readEnd = nullptr;
				writeEnd = nullptr;
				return false;
			}

			return true;
		}

		// Waits for a read already in flight, watching the shell's process handle alongside it.
		// The pipe alone never reports the shell's death — the pseudo-console's host, not the
		// shell, owns the writing end — so the process handle is what lets a killed shell
		// release a blocked reader; without this, Terminate() would kill the shell and a thread
		// in Read would still be waiting on a pipe nothing is ever going to close.
		std::size_t AwaitRead(HANDLE pipe, OVERLAPPED& overlapped, void* process)
		{
			const HANDLE waited[2]{overlapped.hEvent, static_cast<HANDLE>(process)};
			const DWORD count = process != nullptr ? 2u : 1u;

			DWORD signalled = ::WaitForMultipleObjects(count, waited, FALSE, INFINITE);

			if (signalled == WAIT_OBJECT_0 + 1)
			{
				// The process just died. Give the host a short window to deliver whatever it
				// had already flushed before treating this as end-of-stream.
				signalled = ::WaitForSingleObject(overlapped.hEvent, FlushGraceMilliseconds);
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
	}
#endif

	Result<Scope<PseudoConsole>> PseudoConsole::Start(const std::filesystem::path& program,
													  const std::vector<std::string>& arguments,
													  const std::filesystem::path& workingDirectory,
													  std::uint16_t columns, std::uint16_t rows)
	{
#ifdef CORE_PLATFORM_WINDOWS
		HANDLE inputRead = nullptr;
		HANDLE inputWrite = nullptr;
		HANDLE outputRead = nullptr;
		HANDLE outputWrite = nullptr;

		if (!::CreatePipe(&inputRead, &inputWrite, nullptr, 0))
		{
			return Error{ErrorCodeFor(::GetLastError()), "cannot create the shell's input pipe"};
		}

		if (!CreateOverlappedPipe(outputRead, outputWrite))
		{
			const Error error{ErrorCodeFor(::GetLastError()),
							  "cannot create the shell's output pipe"};
			::CloseHandle(inputRead);
			::CloseHandle(inputWrite);
			return error;
		}

		const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
		HPCON console = nullptr;
		const HRESULT created = ::CreatePseudoConsole(size, inputRead, outputWrite, 0, &console);

		// ConPTY duplicates the two ends it was handed, so this side is done with them the
		// moment the call returns. Holding them open would keep the output pipe's writing end
		// alive after the shell exits, and a reader would then block for ever instead of
		// learning that the shell is gone.
		::CloseHandle(inputRead);
		::CloseHandle(outputWrite);

		if (FAILED(created))
		{
			::CloseHandle(inputWrite);
			::CloseHandle(outputRead);
			return Error{ErrorCode::Unsupported, "this system has no pseudo-console support"};
		}

		SIZE_T attributeBytes = 0;
		::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);

		auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
			::HeapAlloc(::GetProcessHeap(), 0, attributeBytes));

		if (attributes == nullptr
			|| !::InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes)
			|| !::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
											console, sizeof(console), nullptr, nullptr))
		{
			const Error error{ErrorCode::IoFailure,
							  "cannot attach the pseudo-console to the process attributes"};

			if (attributes != nullptr)
			{
				::HeapFree(::GetProcessHeap(), 0, attributes);
			}

			::ClosePseudoConsole(console);
			::CloseHandle(inputWrite);
			::CloseHandle(outputRead);
			return error;
		}

		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.lpAttributeList = attributes;

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
		const BOOL started = ::CreateProcessW(
			nullptr, commandLine.data(), nullptr, nullptr, FALSE,
			EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
			directory.empty() ? nullptr : directory.c_str(), &startup.StartupInfo, &information);

		if (started == FALSE)
		{
			const Error error{ErrorCodeFor(::GetLastError()), "cannot start the shell"};
			::DeleteProcThreadAttributeList(attributes);
			::HeapFree(::GetProcessHeap(), 0, attributes);
			::ClosePseudoConsole(console);
			::CloseHandle(inputWrite);
			::CloseHandle(outputRead);
			return error;
		}

		::CloseHandle(information.hThread);

		Scope<PseudoConsole> shell{new PseudoConsole};
		shell->m_Console = console;
		shell->m_Process = information.hProcess;
		shell->m_ToChild = inputWrite;
		shell->m_FromChild = outputRead;
		shell->m_Attributes = attributes;

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

		// The pseudo-console goes first, while the reading end is still open: closing it is the
		// host's cue to shut down, and the flush it does on the way out needs a pipe left to
		// write into.
		if (m_Console != nullptr)
		{
			::ClosePseudoConsole(static_cast<HPCON>(m_Console));
			m_Console = nullptr;
		}

		CloseIfOpen(m_FromChild);
		CloseIfOpen(m_ToChild);

		if (m_Attributes != nullptr)
		{
			auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_Attributes);
			::DeleteProcThreadAttributeList(attributes);
			::HeapFree(::GetProcessHeap(), 0, attributes);
			m_Attributes = nullptr;
		}

		CloseIfOpen(m_Process);
#endif
	}

	std::size_t PseudoConsole::Read(std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_FromChild == nullptr || buffer.empty())
		{
			return 0;
		}

		const HANDLE pipe = static_cast<HANDLE>(m_FromChild);

		OVERLAPPED overlapped{};
		overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

		if (overlapped.hEvent == nullptr)
		{
			return 0;
		}

		DWORD transferred = 0;
		std::size_t read = 0;

		if (::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &transferred,
					   &overlapped))
		{
			read = static_cast<std::size_t>(transferred);
		}
		else if (::GetLastError() == ERROR_IO_PENDING)
		{
			read = AwaitRead(pipe, overlapped, m_Process);
		}

		::CloseHandle(overlapped.hEvent);
		return read;
#else
		(void)buffer;
		return 0;
#endif
	}

	bool PseudoConsole::Write(std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_ToChild == nullptr)
		{
			return false;
		}

		std::size_t written = 0;

		while (written < bytes.size())
		{
			DWORD chunk = 0;

			if (!::WriteFile(static_cast<HANDLE>(m_ToChild), bytes.data() + written,
							 static_cast<DWORD>(bytes.size() - written), &chunk, nullptr)
				|| chunk == 0)
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
		if (m_Process == nullptr)
		{
			return;
		}

		::TerminateProcess(static_cast<HANDLE>(m_Process), 1);
#endif
	}

	bool PseudoConsole::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process == nullptr)
		{
			return false;
		}

		return ::WaitForSingleObject(static_cast<HANDLE>(m_Process), 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
