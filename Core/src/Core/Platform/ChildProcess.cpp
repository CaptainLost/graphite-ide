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

#ifdef CORE_PLATFORM_WINDOWS
	namespace {
		constexpr DWORD kDefaultPipeBufferSize = 0;
		constexpr DWORD kClearedHandleFlags = 0;
		constexpr DWORD kNoFlagsOrAttributes = 0;
		constexpr DWORD kDesiredAccessIgnored = 0;
		constexpr BOOL kInheritable = TRUE;
		constexpr BOOL kInheritListedHandles = TRUE;
		constexpr DWORD kHandleListAttributeCount = 1;
		constexpr UINT kTerminatedExitCode = 0;
		constexpr DWORD kPollWithoutWaiting = 0;
		constexpr const wchar_t* kNullDevice = L"NUL";

		Result<void> CreateInheritablePipe(SECURITY_ATTRIBUTES& inheritable, UniqueHandle& readEnd, UniqueHandle& writeEnd, const char* failureMessage)
		{
			HANDLE createdReadEnd = nullptr;
			HANDLE createdWriteEnd = nullptr;

			const BOOL created = ::CreatePipe(&createdReadEnd, &createdWriteEnd, &inheritable, kDefaultPipeBufferSize);

			if (!created)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = failureMessage};
			}

			readEnd.Reset(createdReadEnd);
			writeEnd.Reset(createdWriteEnd);
			return Result<void>::Ok();
		}
	}
#endif

	Result<Scope<ChildProcess>> ChildProcess::Start([[maybe_unused]] const std::filesystem::path& program,
													[[maybe_unused]] const std::vector<std::string>& arguments,
													[[maybe_unused]] const std::filesystem::path& workingDirectory)
	{
#ifdef CORE_PLATFORM_WINDOWS
		SECURITY_ATTRIBUTES inheritable{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE};

		UniqueHandle childStdinRead;
		UniqueHandle childStdinWrite;
		UniqueHandle childStdoutRead;
		UniqueHandle childStdoutWrite;

		const Result<void> inputPipe = CreateInheritablePipe(inheritable, childStdinRead, childStdinWrite, "cannot create the child's input pipe");

		if (!inputPipe)
		{
			return inputPipe.GetError();
		}

		const Result<void> outputPipe = CreateInheritablePipe(inheritable, childStdoutRead, childStdoutWrite, "cannot create the child's output pipe");

		if (!outputPipe)
		{
			return outputPipe.GetError();
		}

		// The parent's own ends must not stay inheritable, or every child this process starts later
		// inherits them.
		::SetHandleInformation(childStdinWrite.Get(), HANDLE_FLAG_INHERIT, kClearedHandleFlags);
		::SetHandleInformation(childStdoutRead.Get(), HANDLE_FLAG_INHERIT, kClearedHandleFlags);

		// Stderr does not share the output pipe: callers parse stdout as a length-framed protocol,
		// and the child's log lines would land inside message bodies. It goes where this process's
		// own diagnostics go.
		const HANDLE processStdError = ::GetStdHandle(STD_ERROR_HANDLE);
		const bool processHasNoStdError = processStdError == nullptr || processStdError == INVALID_HANDLE_VALUE;
		UniqueHandle childStdError;

		// A windowed process normally has no stderr, and STARTF_USESTDHANDLES needs all three
		// handles valid, so the child gets the null device rather than a null handle that fails far
		// from here. A real console handle is duplicated, so that the copy is inheritable and
		// closing it leaves this process's own stderr alone.
		if (processHasNoStdError)
		{
			childStdError.Reset(::CreateFileW(kNullDevice, GENERIC_WRITE,
											  FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
											  OPEN_EXISTING, kNoFlagsOrAttributes, nullptr));

			if (!childStdError)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
							 .Message = "cannot open the null device for the child's stderr"};
			}
		}
		else
		{
			HANDLE duplicate = nullptr;
			const HANDLE currentProcess = ::GetCurrentProcess();

			const BOOL duplicated = ::DuplicateHandle(currentProcess, processStdError, currentProcess, &duplicate, kDesiredAccessIgnored, kInheritable, DUPLICATE_SAME_ACCESS);

			if (!duplicated)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
							 .Message = "cannot duplicate the process's stderr for the child"};
			}

			childStdError.Reset(duplicate);
		}

		// Without an explicit list CreateProcessW hands the child every inheritable handle this
		// process holds at that instant, including ones of unrelated work on other threads. The
		// list requires distinct handles, which these three are.
		std::array<HANDLE, 3> inheritedHandles{childStdinRead.Get(), childStdoutWrite.Get(),
											   childStdError.Get()};

		ProcThreadAttributeList attributeList;

		const bool listInitialised = attributeList.Initialise(kHandleListAttributeCount);

		if (!listInitialised)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "cannot prepare the child's handle list"};
		}

		const bool listUpdated = attributeList.Update(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles.data(), inheritedHandles.size() * sizeof(HANDLE));

		if (!listUpdated)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
						 .Message = "cannot restrict the child's inherited handles"};
		}

		// Terminate ends the job rather than the process, so a child that is only a launcher for
		// another process cannot leave that process holding our pipes open.
		UniqueHandle job{::CreateJobObjectW(nullptr, nullptr)};

		if (!job)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "cannot create a job for the child"};
		}

		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

		if (::SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "cannot set up the child's job"};
		}

		STARTUPINFOEXW startupInfo{
			.StartupInfo = {
				.cb = sizeof(STARTUPINFOEXW),
				.dwFlags = STARTF_USESTDHANDLES,
				.hStdInput = childStdinRead.Get(),
				.hStdOutput = childStdoutWrite.Get(),
				.hStdError = childStdError.Get(),
			},
			.lpAttributeList = attributeList.Get(),
		};

		PROCESS_INFORMATION processInformation{};

		std::wstring commandLine = BuildWindowsCommandLine(program, arguments);
		const std::wstring nativeWorkingDirectory = workingDirectory.wstring();

		const BOOL started = ::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, kInheritListedHandles,
											  CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
											  nullptr, nativeWorkingDirectory.c_str(), &startupInfo.StartupInfo,
											  &processInformation);

		if (!started)
		{
			return Error{.Code = ErrorCodeFromWin32(::GetLastError()),
						 .Message = std::format("cannot start '{}'", ToUtf8(program))};
		}

		UniqueHandle primaryThread{processInformation.hThread};
		UniqueHandle processHandle{processInformation.hProcess};

		// Assigned while suspended, so nothing the child starts can run outside the job.
		if (::AssignProcessToJobObject(job.Get(), processHandle.Get()) == FALSE)
		{
			const DWORD assignErrorCode = ::GetLastError();
			::TerminateProcess(processHandle.Get(), kTerminatedExitCode);

			return Error{.Code = ErrorCodeFromWin32(assignErrorCode),
						 .Message = std::format("cannot place '{}' in its job", ToUtf8(program))};
		}

		::ResumeThread(primaryThread.Get());

		// The parent's copies of the child's ends close now: kept open, they would stop the pipes
		// from signalling closure when the child exits.
		childStdinRead.Reset();
		childStdoutWrite.Reset();
		childStdError.Reset();
		primaryThread.Reset();

		Scope<ChildProcess> child{new ChildProcess()};
		child->m_Job = std::move(job);
		child->m_Process = std::move(processHandle);
		child->m_ToChild = std::move(childStdinWrite);
		child->m_FromChild = std::move(childStdoutRead);

		return child;
#else
		return Error{.Code = ErrorCode::Unsupported,
					 .Message = "starting a child process is not implemented on this platform"};
#endif
	}

	ChildProcess::~ChildProcess()
	{
		Terminate();
	}

	std::size_t ChildProcess::Read([[maybe_unused]] std::span<char> buffer)
	{
#ifdef CORE_PLATFORM_WINDOWS
		DWORD bytesRead = 0;

		// ReadFile on a pipe blocks until at least one byte arrives or the writing end closes, so
		// zero here always means the child is gone, never "nothing yet".
		const BOOL readSucceeded = ::ReadFile(m_FromChild.Get(), buffer.data(),
											  static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);

		if (!readSucceeded)
		{
			return 0;
		}

		return static_cast<std::size_t>(bytesRead);
#else
		return 0;
#endif
	}

	bool ChildProcess::Write([[maybe_unused]] std::string_view bytes)
	{
#ifdef CORE_PLATFORM_WINDOWS
		std::size_t bytesWritten = 0;

		while (bytesWritten < bytes.size())
		{
			DWORD bytesWrittenThisCall = 0;
			const BOOL writeSucceeded =
				::WriteFile(m_ToChild.Get(), bytes.data() + bytesWritten,
							static_cast<DWORD>(bytes.size() - bytesWritten), &bytesWrittenThisCall, nullptr);

			if (!writeSucceeded)
			{
				return false;
			}

			bytesWritten += bytesWrittenThisCall;
		}

		return true;
#else
		return false;
#endif
	}

	void ChildProcess::Terminate()
	{
#ifdef CORE_PLATFORM_WINDOWS
		// Fails on a tree that has already exited, which is harmless, so the result is ignored.
		if (m_Job)
		{
			::TerminateJobObject(m_Job.Get(), kTerminatedExitCode);
		}
#else
		// Empty because Start refuses on this platform, so no child exists to end. A port that
		// implements Start must implement this in the same change, or a caller's join after
		// Terminate waits on a read nothing releases.
#endif
	}

	bool ChildProcess::IsRunning() const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return false;
		}

		// Not GetExitCodeProcess: a child may exit with 259, which is also STILL_ACTIVE, the code
		// it reports for a process that has not ended. The process object is signalled once it ends.
		return ::WaitForSingleObject(m_Process.Get(), kPollWithoutWaiting) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}
}
