#pragma once

#include "Core/Platform/Process.h"
#include "Core/Platform/UniqueHandle.h"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <windows.h>

// Every Windows has it, so a test can start a process that writes its own id where the test reads it.
inline std::filesystem::path WindowsPowerShell()
{
	const std::filesystem::path systemRoot = Core::EnvironmentPath(L"SystemRoot").value_or(std::filesystem::path{L"C:\\Windows"});

	return systemRoot / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe";
}

// A process a test leaves running on purpose. It writes its id, followed by a line end, to a file the test names.
// This object is made before that process is started, so that its id can be checked against what the test started,
// and it ends the process, waiting until it has ended, on every way out of the test, a failed REQUIRE included, so
// that the folder the process runs in can be removed after it.
class LeftBehindProcess
{
public:
	LeftBehindProcess(std::filesystem::path idFile, std::filesystem::path program)
		: m_IdFile(std::move(idFile)), m_Program(std::move(program)), m_CreatedNoEarlierThan(AMomentAgo())
	{
	}

	~LeftBehindProcess()
	{
		constexpr std::chrono::seconds kLateIdDeadline{15};
		constexpr UINT kEndedByTheTest = 0;
		constexpr DWORD kTeardownWaitMilliseconds = 10000;

		if (!m_Process)
		{
			WaitUntilFound(kLateIdDeadline);
		}

		if (m_Process)
		{
			::TerminateProcess(m_Process.Get(), kEndedByTheTest);
			::WaitForSingleObject(m_Process.Get(), kTeardownWaitMilliseconds);
		}
	}

	LeftBehindProcess(const LeftBehindProcess&) = delete;
	LeftBehindProcess& operator=(const LeftBehindProcess&) = delete;

	// True once the id has come and its process is shown to be the one the test started: it runs the program and was
	// created after this object. An id that fails either check, as an id reused by an unrelated process would, opens
	// nothing, so nothing unrelated is ever ended.
	bool WaitUntilFound(std::chrono::seconds deadline)
	{
		constexpr std::chrono::milliseconds kPollInterval{50};
		const std::chrono::steady_clock::time_point giveUpAt = std::chrono::steady_clock::now() + deadline;

		while (!m_Process && std::chrono::steady_clock::now() < giveUpAt)
		{
			const std::optional<std::uint32_t> processId = WrittenId();

			if (processId.has_value())
			{
				OpenIfTheTestStartedIt(*processId);
				break;
			}

			std::this_thread::sleep_for(kPollInterval);
		}

		return static_cast<bool>(m_Process);
	}

	bool IsRunning() const
	{
		constexpr DWORD kPollWithoutWaiting = 0;

		return m_Process && ::WaitForSingleObject(m_Process.Get(), kPollWithoutWaiting) == WAIT_TIMEOUT;
	}

	bool EndsWithin(std::chrono::milliseconds timeout) const
	{
		return m_Process && ::WaitForSingleObject(m_Process.Get(), static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0;
	}

private:
	// WARNING: The coarse clock this reads may stand a tick apart from the one that stamps a process's creation, so the
	// mark is set a second early.
	static FILETIME AMomentAgo()
	{
		constexpr ULONGLONG kOneSecondInFileTimeUnits = 10'000'000;

		FILETIME now{};
		::GetSystemTimeAsFileTime(&now);

		ULARGE_INTEGER units{};
		units.LowPart = now.dwLowDateTime;
		units.HighPart = now.dwHighDateTime;
		units.QuadPart -= kOneSecondInFileTimeUnits;

		return FILETIME{.dwLowDateTime = units.LowPart, .dwHighDateTime = units.HighPart};
	}

	std::optional<std::uint32_t> WrittenId() const
	{
		std::ifstream stream{m_IdFile, std::ios::binary};
		const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		const bool holdsWholeId = !text.empty() && text.back() == '\n';

		std::uint32_t processId = 0;
		const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), processId);

		if (!holdsWholeId || parsed.ec != std::errc{} || processId == 0)
		{
			return std::nullopt;
		}

		return processId;
	}

	void OpenIfTheTestStartedIt(std::uint32_t processId)
	{
		constexpr BOOL kNotInherited = FALSE;
		constexpr DWORD kNativePathFormat = 0;
		constexpr BOOL kIgnoreCase = TRUE;

		Core::UniqueHandle opened{::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, kNotInherited, processId)};

		if (!opened)
		{
			return;
		}

		FILETIME created{};
		FILETIME exited{};
		FILETIME kernelTime{};
		FILETIME userTime{};
		const bool createdAfterThis = ::GetProcessTimes(opened.Get(), &created, &exited, &kernelTime, &userTime) != FALSE &&
									  ::CompareFileTime(&created, &m_CreatedNoEarlierThan) >= 0;

		constexpr std::size_t kLongestImagePath = 32768;
		std::wstring imagePath(kLongestImagePath, L'\0');
		DWORD imagePathLength = static_cast<DWORD>(imagePath.size());
		const bool imageRead = ::QueryFullProcessImageNameW(opened.Get(), kNativePathFormat, imagePath.data(), &imagePathLength) != FALSE;
		const std::wstring expectedImage = m_Program.wstring();
		const bool runsTheProgram = imageRead && ::CompareStringOrdinal(imagePath.data(), static_cast<int>(imagePathLength), expectedImage.c_str(),
																		  static_cast<int>(expectedImage.size()), kIgnoreCase) == CSTR_EQUAL;

		if (createdAfterThis && runsTheProgram)
		{
			m_Process = std::move(opened);
		}
	}

	std::filesystem::path m_IdFile;
	std::filesystem::path m_Program;
	FILETIME m_CreatedNoEarlierThan{};
	Core::UniqueHandle m_Process;
};
