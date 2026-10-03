#include "corepch.h"

#include "Core/Platform/ChildProcess.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/CommandLine.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Owners.h"
#include "Core/Platform/Win32Error.h"

#ifdef CORE_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace Core {

	Result<Scope<ChildProcess>> ChildProcess::Start([[maybe_unused]] const std::filesystem::path& program,
													[[maybe_unused]] const std::vector<std::string>& arguments,
													[[maybe_unused]] const std::filesystem::path& workingDirectory)
	{
#ifdef CORE_PLATFORM_WINDOWS
		SECURITY_ATTRIBUTES inheritable{};
		inheritable.nLength = sizeof(inheritable);
		inheritable.bInheritHandle = TRUE;

		UniqueHandle childStdinRead;
		UniqueHandle childStdinWrite;
		UniqueHandle childStdoutRead;
		UniqueHandle childStdoutWrite;

		{
			HANDLE read = nullptr;
			HANDLE write = nullptr;

			if (!::CreatePipe(&read, &write, &inheritable, 0))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot create the child's input pipe"};
			}

			childStdinRead.Reset(read);
			childStdinWrite.Reset(write);
		}

		{
			HANDLE read = nullptr;
			HANDLE write = nullptr;

			if (!::CreatePipe(&read, &write, &inheritable, 0))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot create the child's output pipe"};
			}

			childStdoutRead.Reset(read);
			childStdoutWrite.Reset(write);
		}

		// Only the ends the child inherits should be inheritable: leaving the parent's own
		// ends inheritable would leak them into every future child this process starts.
		::SetHandleInformation(childStdinWrite.Get(), HANDLE_FLAG_INHERIT, 0);
		::SetHandleInformation(childStdoutRead.Get(), HANDLE_FLAG_INHERIT, 0);

		// This class exists to carry a framed protocol on stdout: a later caller parses that
		// stream by a length prefix, and anything else written into the same pipe is
		// indistinguishable from protocol content. Redirecting stderr here too would interleave
		// the child's log lines into message bodies, so leave it on the parent's own stderr,
		// where diagnostics already go.
		const HANDLE processStdError = ::GetStdHandle(STD_ERROR_HANDLE);
		UniqueHandle childStdError;

		// Graphite is a windowed application with no console attached, so this is the normal
		// case in the shipped product, not a rare one: GetStdHandle returns NULL there almost
		// always. STARTF_USESTDHANDLES requires all three handles to be valid, and a child
		// handed a null one gets a broken stderr that fails far from here in ways that look
		// unrelated to this. Opening the null device keeps the child's handle valid regardless
		// of whether this process has a console; a real console handle, when there is one, is
		// duplicated into an inheritable copy so a server's diagnostics still land there during
		// development. Either way the handle is this call's own, so the process's real stderr
		// handle is never modified or closed here.
		if (processStdError == nullptr || processStdError == INVALID_HANDLE_VALUE)
		{
			childStdError.Reset(::CreateFileW(L"NUL", GENERIC_WRITE,
											  FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
											  OPEN_EXISTING, 0, nullptr));

			if (!childStdError)
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot open the null device for the child's stderr"};
			}
		}
		else
		{
			HANDLE duplicate = nullptr;
			const HANDLE self = ::GetCurrentProcess();

			if (!::DuplicateHandle(self, processStdError, self, &duplicate, 0, TRUE,
								   DUPLICATE_SAME_ACCESS))
			{
				return Error{ErrorCodeFromWin32(::GetLastError()),
							 "cannot duplicate the process's stderr for the child"};
			}

			childStdError.Reset(duplicate);
		}

		// Without an explicit list CreateProcessW hands the child every inheritable handle this
		// process holds at that instant, including ones belonging to unrelated work on other
		// threads. The list names exactly the three the child is meant to have; all are distinct
		// handles this call owns, as the list requires.
		std::array<HANDLE, 3> inheritedHandles{childStdinRead.Get(), childStdoutWrite.Get(),
											   childStdError.Get()};

		ProcThreadAttributeList attributeList;

		if (!attributeList.Initialise(1))
		{
			return Error{ErrorCodeFromWin32(::GetLastError()), "cannot prepare the child's handle list"};
		}

		if (!attributeList.Update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles.data(),
								  inheritedHandles.size() * sizeof(HANDLE)))
		{
			return Error{ErrorCodeFromWin32(::GetLastError()),
						 "cannot restrict the child's inherited handles"};
		}

		STARTUPINFOEXW startupInfo{};
		startupInfo.StartupInfo.cb = sizeof(startupInfo);
		startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.StartupInfo.hStdInput = childStdinRead.Get();
		startupInfo.StartupInfo.hStdOutput = childStdoutWrite.Get();
		startupInfo.StartupInfo.hStdError = childStdError.Get();
		startupInfo.lpAttributeList = attributeList.Get();

		PROCESS_INFORMATION processInfo{};

		std::wstring commandLine = BuildWindowsCommandLine(program, arguments);
		const std::wstring directory = workingDirectory.wstring();

		if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
							  CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
							  directory.c_str(), &startupInfo.StartupInfo, &processInfo))
		{
			return Error{ErrorCodeFromWin32(::GetLastError()),
						 std::format("cannot start '{}'", ToUtf8(program))};
		}

		UniqueHandle thread{processInfo.hThread};
		UniqueHandle processHandle{processInfo.hProcess};

		// The parent never reads or writes through these ends; the child owns them now, and
		// keeping the parent's copy open would stop the pipe from signalling closure. The
		// stderr copy is the same story, whether it is the null device or a duplicate of the
		// console handle.
		childStdinRead.Reset();
		childStdoutWrite.Reset();
		childStdError.Reset();
		thread.Reset();

		Scope<ChildProcess> process{new ChildProcess()};
		process->m_Process = std::move(processHandle);
		process->m_ToChild = std::move(childStdinWrite);
		process->m_FromChild = std::move(childStdoutRead);

		return process;
#else
		return Error{ErrorCode::Unsupported,
					 "starting a child process is not implemented on this platform"};
#endif
	}

	ChildProcess::~ChildProcess()
	{
#ifdef CORE_PLATFORM_WINDOWS
		// The handles close as the members are destroyed; only the process needs ending first.
		if (m_Process)
		{
			::TerminateProcess(m_Process.Get(), 0);
		}
#endif
	}

	std::size_t ChildProcess::Read([[maybe_unused]] std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		DWORD read = 0;

		// ReadFile blocks until the pipe has at least one byte or the writing end closes, which
		// is exactly the contract callers depend on: zero means gone, never "nothing yet".
		if (!::ReadFile(m_FromChild.Get(), buffer.data(),
						static_cast<DWORD>(buffer.size()), &read, nullptr))
		{
			return 0;
		}

		return static_cast<std::size_t>(read);
#else
		return 0;
#endif
	}

	bool ChildProcess::Write([[maybe_unused]] std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		std::size_t written = 0;

		while (written < bytes.size())
		{
			DWORD wroteThisCall = 0;
			const BOOL ok =
				::WriteFile(m_ToChild.Get(), bytes.data() + written,
							static_cast<DWORD>(bytes.size() - written), &wroteThisCall, nullptr);

			if (!ok)
			{
				return false;
			}

			written += wroteThisCall;
		}

		return true;
#else
		return false;
#endif
	}

	void ChildProcess::Terminate()
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (m_Process)
		{
			// Only the process is touched: the handle another thread may be reading through is
			// left open, and the destructor still closes everything exactly once. Terminating an
			// already-dead child simply fails, which is why the return value is ignored.
			::TerminateProcess(m_Process.Get(), 0);
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
		if (!m_Process)
		{
			return false;
		}

		// The exit code cannot answer this: a process may legitimately exit with 259, the value
		// GetExitCodeProcess reports (as STILL_ACTIVE) for one that has not ended. The process
		// object is signalled once it has ended, so a zero-timeout wait is the unambiguous test.
		return ::WaitForSingleObject(m_Process.Get(), 0) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
