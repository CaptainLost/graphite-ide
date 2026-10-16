#include "corepch.h"

#include "Core/Platform/ChildProcess.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Platform/CommandLine.h"
#include "Core/Platform/UniqueHandle.h"
#include "Core/Platform/Win32Owners.h"
#include "Core/Platform/Win32Error.h"
#include "Core/Platform/Win32Io.h"

#include <algorithm>
#include <chrono>
#include <cwchar>

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

		// INFINITE is one past it, and a wait asked for in milliseconds is never meant to be endless.
		constexpr long long kLongestFiniteWait = INFINITE - 1;
		constexpr const wchar_t* kNullDevice = L"NUL";
		constexpr DWORD kResumeFailed = static_cast<DWORD>(-1);
		constexpr BOOL kIgnoreCase = TRUE;
		constexpr wchar_t kNameEnd = L'=';

		bool NamesVariable(const std::wstring& entry, const std::wstring& name)
		{
			return entry.size() > name.size() && entry[name.size()] == kNameEnd &&
				   ::CompareStringOrdinal(entry.data(), static_cast<int>(name.size()), name.data(), static_cast<int>(name.size()),
										  kIgnoreCase) == CSTR_EQUAL;
		}

		// A name ends at the first '=' after the first character: the drive-current-directory entries
		// begin with one, as in "=C:=C:/dir".
		int NameLength(const std::wstring& entry)
		{
			const std::size_t nameEnd = entry.find(kNameEnd, 1);

			return static_cast<int>(nameEnd == std::wstring::npos ? entry.size() : nameEnd);
		}

		bool ComesFirstIgnoringCase(const std::wstring& left, const std::wstring& right)
		{
			return ::CompareStringOrdinal(left.data(), NameLength(left), right.data(), NameLength(right), kIgnoreCase) == CSTR_LESS_THAN;
		}

		// A name ending early or a value cut short would set some other variable than the caller asked for, or none.
		bool IsWellFormed(const EnvironmentVariable& variable)
		{
			const bool nameEndsAtItsEnd = variable.Name.find('=', 1) == std::string::npos;
			const bool holdsNoNull = variable.Name.find('\0') == std::string::npos && variable.Value.find('\0') == std::string::npos;

			return !variable.Name.empty() && nameEndsAtItsEnd && holdsNoNull;
		}

		// CreateProcessW wants each "NAME=value" ended by a null, the block ended by one more, sorted by name ignoring case.
		Result<std::wstring> EnvironmentBlockWith(std::span<const EnvironmentVariable> overrides)
		{
			for (const EnvironmentVariable& variable : overrides)
			{
				if (!IsWellFormed(variable))
				{
					return Error{.Code = ErrorCode::InvalidArgument,
								 .Message = "an environment variable needs a name without '=' and a name and value without a null"};
				}
			}

			wchar_t* inherited = ::GetEnvironmentStringsW();

			if (inherited == nullptr)
			{
				return Error{.Code = ErrorCodeFromWin32(::GetLastError()), .Message = "cannot read this process's environment"};
			}

			std::vector<std::wstring> entries;

			for (const wchar_t* entry = inherited; *entry != L'\0'; entry += std::wcslen(entry) + 1)
			{
				entries.emplace_back(entry);
			}

			::FreeEnvironmentStringsW(inherited);

			for (const EnvironmentVariable& variable : overrides)
			{
				const std::wstring name = ToUtf16(variable.Name);

				std::erase_if(entries, [&name](const std::wstring& entry) { return NamesVariable(entry, name); });
				entries.push_back(name + kNameEnd + ToUtf16(variable.Value));
			}

			std::sort(entries.begin(), entries.end(), ComesFirstIgnoringCase);

			std::wstring block;

			for (const std::wstring& entry : entries)
			{
				block += entry;
				block.push_back(L'\0');
			}

			block.push_back(L'\0');
			return block;
		}

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
													[[maybe_unused]] const std::filesystem::path& workingDirectory,
													[[maybe_unused]] std::span<const EnvironmentVariable> environment)
	{
#ifdef CORE_PLATFORM_WINDOWS
		std::wstring environmentBlock;
		LPVOID environmentPointer = nullptr;
		DWORD creationFlags = CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;

		if (!environment.empty())
		{
			Result<std::wstring> block = EnvironmentBlockWith(environment);

			if (!block)
			{
				return block.GetError();
			}

			environmentBlock = std::move(block).GetValue();
			environmentPointer = environmentBlock.data();
			creationFlags |= CREATE_UNICODE_ENVIRONMENT;
		}

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

		const BOOL started = ::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, kInheritListedHandles, creationFlags,
											  environmentPointer, nativeWorkingDirectory.c_str(), &startupInfo.StartupInfo,
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

		if (::ResumeThread(primaryThread.Get()) == kResumeFailed)
		{
			const DWORD resumeErrorCode = ::GetLastError();
			::TerminateJobObject(job.Get(), kTerminatedExitCode);

			return Error{.Code = ErrorCodeFromWin32(resumeErrorCode), .Message = std::format("cannot resume '{}'", ToUtf8(program))};
		}

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
		return WriteAll(m_ToChild.Get(), bytes);
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

		JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};

		if (m_Job && ::QueryInformationJobObject(m_Job.Get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting),
												 nullptr) != FALSE)
		{
			return accounting.ActiveProcesses > 0;
		}

		// Not GetExitCodeProcess: a child may exit with 259, which is also STILL_ACTIVE, the code
		// it reports for a process that has not ended. The process object is signalled once it ends.
		return ::WaitForSingleObject(m_Process.Get(), kPollWithoutWaiting) == WAIT_TIMEOUT;
#else
		return false;
#endif
	}

	bool ChildProcess::WaitForExit([[maybe_unused]] std::chrono::milliseconds timeout) const
	{
#ifdef CORE_PLATFORM_WINDOWS
		if (!m_Process)
		{
			return true;
		}

		const std::chrono::milliseconds wait =
			std::clamp(timeout, std::chrono::milliseconds{0}, std::chrono::milliseconds{kLongestFiniteWait});
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + wait;
		std::chrono::milliseconds remaining = wait;

		// WARNING: A wait can end up to a clock tick before its timeout, so what is left is waited again until the
		// deadline has passed: a caller that gave the child long enough would otherwise act early.
		while (true)
		{
			const DWORD waitResult = ::WaitForSingleObject(m_Process.Get(), static_cast<DWORD>(remaining.count()));

			if (waitResult != WAIT_TIMEOUT)
			{
				return waitResult == WAIT_OBJECT_0;
			}

			const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

			if (now >= deadline)
			{
				return false;
			}

			remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
		}
#else
		return true;
#endif
	}
}
