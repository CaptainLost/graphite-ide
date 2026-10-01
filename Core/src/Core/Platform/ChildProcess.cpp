#include "corepch.h"

#include "Core/Platform/ChildProcess.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Platform/CommandLine.h"

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

		// This class exists to carry a framed protocol on stdout: a later caller parses that
		// stream by a length prefix, and anything else written into the same pipe is
		// indistinguishable from protocol content. Redirecting stderr here too would interleave
		// the child's log lines into message bodies, so leave it on the parent's own stderr,
		// where diagnostics already go.
		HANDLE childStdError = ::GetStdHandle(STD_ERROR_HANDLE);
		bool ownsChildStdError = true;

		// Graphite is a windowed application with no console attached, so this is the normal
		// case in the shipped product, not a rare one: GetStdHandle returns NULL there almost
		// always. STARTF_USESTDHANDLES requires all three handles to be valid, and a child
		// handed a null one gets a broken stderr that fails far from here in ways that look
		// unrelated to this. Opening the null device keeps the child's handle valid regardless
		// of whether this process has a console; a real console handle, when there is one, is
		// duplicated into an inheritable copy so a server's diagnostics still land there during
		// development. The copy is this call's own, so the process's real stderr handle is never
		// modified.
		if (childStdError == nullptr || childStdError == INVALID_HANDLE_VALUE)
		{
			childStdError = ::CreateFileW(L"NUL", GENERIC_WRITE,
										  FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
										  OPEN_EXISTING, 0, nullptr);

			if (childStdError == INVALID_HANDLE_VALUE)
			{
				const Error error{ErrorCodeFor(::GetLastError()),
								  "cannot open the null device for the child's stderr"};

				::CloseHandle(childStdinRead);
				::CloseHandle(childStdinWrite);
				::CloseHandle(childStdoutRead);
				::CloseHandle(childStdoutWrite);

				return error;
			}
		}
		else
		{
			HANDLE duplicate = nullptr;
			const HANDLE self = ::GetCurrentProcess();

			if (!::DuplicateHandle(self, childStdError, self, &duplicate, 0, TRUE,
								   DUPLICATE_SAME_ACCESS))
			{
				const Error error{ErrorCodeFor(::GetLastError()),
								  "cannot duplicate the process's stderr for the child"};

				::CloseHandle(childStdinRead);
				::CloseHandle(childStdinWrite);
				::CloseHandle(childStdoutRead);
				::CloseHandle(childStdoutWrite);

				return error;
			}

			childStdError = duplicate;
		}

		// Without an explicit list CreateProcessW hands the child every inheritable handle this
		// process holds at that instant, including ones belonging to unrelated work on other
		// threads. The list names exactly the three the child is meant to have; all are distinct
		// handles this call owns, as the list requires.
		std::array<HANDLE, 3> inheritedHandles{childStdinRead, childStdoutWrite, childStdError};

		const auto closeAll = [&]
		{
			::CloseHandle(childStdinRead);
			::CloseHandle(childStdinWrite);
			::CloseHandle(childStdoutRead);
			::CloseHandle(childStdoutWrite);

			if (ownsChildStdError)
			{
				::CloseHandle(childStdError);
			}
		};

		SIZE_T attributeListSize = 0;
		::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);

		std::vector<std::byte> attributeStorage(attributeListSize);
		auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());

		if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize))
		{
			const Error error{ErrorCodeFor(::GetLastError()),
							  "cannot prepare the child's handle list"};
			closeAll();
			return error;
		}

		if (!::UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
										 inheritedHandles.data(),
										 inheritedHandles.size() * sizeof(HANDLE), nullptr, nullptr))
		{
			const Error error{ErrorCodeFor(::GetLastError()),
							  "cannot restrict the child's inherited handles"};
			::DeleteProcThreadAttributeList(attributeList);
			closeAll();
			return error;
		}

		STARTUPINFOEXW startupInfo{};
		startupInfo.StartupInfo.cb = sizeof(startupInfo);
		startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.StartupInfo.hStdInput = childStdinRead;
		startupInfo.StartupInfo.hStdOutput = childStdoutWrite;
		startupInfo.StartupInfo.hStdError = childStdError;
		startupInfo.lpAttributeList = attributeList;

		PROCESS_INFORMATION processInfo{};

		std::wstring commandLine = BuildWindowsCommandLine(program, arguments);
		const std::wstring directory = workingDirectory.wstring();

		const BOOL created = ::CreateProcessW(
			nullptr, commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(),
			&startupInfo.StartupInfo, &processInfo);

		// Capture before DeleteProcThreadAttributeList can overwrite the thread's last error.
		const DWORD createError = created ? ERROR_SUCCESS : ::GetLastError();
		::DeleteProcThreadAttributeList(attributeList);

		if (!created)
		{
			const Error error{ErrorCodeFor(createError),
							  std::format("cannot start '{}'", ToUtf8(program))};
			closeAll();
			return error;
		}

		// The parent never reads or writes through these ends; the child owns them now, and
		// keeping the parent's copy open would stop the pipe from signalling closure. The
		// null-device handle, when this process opened one, is the same story; the real console
		// stderr handle, when GetStdHandle supplied it, belongs to the process rather than to
		// this call and is never closed here.
		::CloseHandle(childStdinRead);
		::CloseHandle(childStdoutWrite);
		::CloseHandle(processInfo.hThread);

		if (ownsChildStdError)
		{
			::CloseHandle(childStdError);
		}

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

	void ChildProcess::Terminate()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process != nullptr)
		{
			// Only the process is touched: the handle another thread may be reading through is
			// left open, and the destructor still closes everything exactly once. Terminating an
			// already-dead child simply fails, which is why the return value is ignored.
			::TerminateProcess(static_cast<HANDLE>(m_Process), 0);
		}
#else
		// Deliberately empty rather than absent, so the reason is written down where the next
		// port will read it: Start already refuses on every other platform, so nothing here can
		// hold a live child, and a thread blocked in Read is a thread that never started. A port
		// that implements Start must implement this in the same change — without it the join a
		// caller does after Terminate would wait on a read nothing releases.
#endif
	}

	bool ChildProcess::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process == nullptr)
		{
			return false;
		}

		// The exit code cannot answer this: a process may legitimately exit with 259, the value
		// GetExitCodeProcess reports (as STILL_ACTIVE) for one that has not ended. The process
		// object is signalled once it has ended, so a zero-timeout wait is the unambiguous test.
		return ::WaitForSingleObject(static_cast<HANDLE>(m_Process), 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
