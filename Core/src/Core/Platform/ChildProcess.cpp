#include "corepch.h"

#include "Core/Platform/ChildProcess.h"

#include "Core/Fs/FileSystem.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
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

		// CreateProcessW takes one command line rather than an argv array, so the pieces have
		// to be joined here. Quoting only when a space is present keeps the common case (plain
		// flags such as /c) readable in a debugger without changing what the child receives.
		std::wstring BuildCommandLine(const std::filesystem::path& program,
									  const std::vector<std::string>& arguments)
		{
			std::wstring commandLine = program.wstring();

			for (const std::string& argument : arguments)
			{
				const std::wstring wide{argument.begin(), argument.end()};

				commandLine.push_back(L' ');

				if (wide.find(L' ') != std::wstring::npos)
				{
					commandLine.push_back(L'"');
					commandLine += wide;
					commandLine.push_back(L'"');
				}
				else
				{
					commandLine += wide;
				}
			}

			return commandLine;
		}
	}
#endif

	Result<Scope<ChildProcess>> ChildProcess::Start(const std::filesystem::path& program,
													 const std::vector<std::string>& arguments,
													 const std::filesystem::path& workingDirectory)
	{
#ifdef CORE_PLATFORM_WINDOWS
		SECURITY_ATTRIBUTES inheritable{};
		inheritable.nLength = sizeof(inheritable);
		inheritable.bInheritHandle = TRUE;

		HANDLE childStdinRead = nullptr;
		HANDLE childStdinWrite = nullptr;

		if (!::CreatePipe(&childStdinRead, &childStdinWrite, &inheritable, 0))
		{
			return Error{ErrorCodeFor(::GetLastError()), "cannot create the child's input pipe"};
		}

		HANDLE childStdoutRead = nullptr;
		HANDLE childStdoutWrite = nullptr;

		if (!::CreatePipe(&childStdoutRead, &childStdoutWrite, &inheritable, 0))
		{
			const Error error{ErrorCodeFor(::GetLastError()),
							  "cannot create the child's output pipe"};
			::CloseHandle(childStdinRead);
			::CloseHandle(childStdinWrite);
			return error;
		}

		// Only the ends the child inherits should be inheritable: leaving the parent's own
		// ends inheritable would leak them into every future child this process starts.
		::SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0);
		::SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdInput = childStdinRead;
		startupInfo.hStdOutput = childStdoutWrite;
		startupInfo.hStdError = childStdoutWrite;

		PROCESS_INFORMATION processInfo{};

		std::wstring commandLine = BuildCommandLine(program, arguments);
		const std::wstring directory = workingDirectory.wstring();

		const BOOL created = ::CreateProcessW(
			nullptr, commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW, nullptr, directory.c_str(), &startupInfo, &processInfo);

		if (!created)
		{
			const Error error{ErrorCodeFor(::GetLastError()),
							  std::format("cannot start '{}'", ToUtf8(program))};

			::CloseHandle(childStdinRead);
			::CloseHandle(childStdinWrite);
			::CloseHandle(childStdoutRead);
			::CloseHandle(childStdoutWrite);

			return error;
		}

		// The parent never reads or writes through these ends; the child owns them now, and
		// keeping the parent's copy open would stop the pipe from signalling closure.
		::CloseHandle(childStdinRead);
		::CloseHandle(childStdoutWrite);
		::CloseHandle(processInfo.hThread);

		Scope<ChildProcess> process{new ChildProcess()};
		process->m_Process = processInfo.hProcess;
		process->m_ToChild = childStdinWrite;
		process->m_FromChild = childStdoutRead;

		return process;
#else
		(void)program;
		(void)arguments;
		(void)workingDirectory;
		return Error{ErrorCode::Unsupported,
					 "starting a child process is not implemented on this platform"};
#endif
	}

	ChildProcess::~ChildProcess()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process != nullptr)
		{
			::TerminateProcess(static_cast<HANDLE>(m_Process), 0);
			::CloseHandle(static_cast<HANDLE>(m_Process));
		}

		if (m_ToChild != nullptr)
		{
			::CloseHandle(static_cast<HANDLE>(m_ToChild));
		}

		if (m_FromChild != nullptr)
		{
			::CloseHandle(static_cast<HANDLE>(m_FromChild));
		}
#endif
	}

	std::size_t ChildProcess::Read(std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		DWORD read = 0;

		// ReadFile blocks until the pipe has at least one byte or the writing end closes, which
		// is exactly the contract callers depend on: zero means gone, never "nothing yet".
		if (!::ReadFile(static_cast<HANDLE>(m_FromChild), buffer.data(),
						static_cast<DWORD>(buffer.size()), &read, nullptr))
		{
			return 0;
		}

		return static_cast<std::size_t>(read);
#else
		(void)buffer;
		return 0;
#endif
	}

	bool ChildProcess::Write(std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		std::size_t written = 0;

		while (written < bytes.size())
		{
			DWORD wroteThisCall = 0;
			const BOOL ok =
				::WriteFile(static_cast<HANDLE>(m_ToChild), bytes.data() + written,
						   static_cast<DWORD>(bytes.size() - written), &wroteThisCall, nullptr);

			if (!ok)
			{
				return false;
			}

			written += wroteThisCall;
		}

		return true;
#else
		(void)bytes;
		return false;
#endif
	}

	bool ChildProcess::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process == nullptr)
		{
			return false;
		}

		DWORD exitCode = 0;

		if (!::GetExitCodeProcess(static_cast<HANDLE>(m_Process), &exitCode))
		{
			return false;
		}

		return exitCode == STILL_ACTIVE;
#else
		return false;
#endif
	}
}
