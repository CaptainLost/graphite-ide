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

#include <windows.h>

// Every Windows has it, so a test can start a process that writes its own id where the test reads it.
inline std::filesystem::path WindowsPowerShell()
{
	const std::filesystem::path systemRoot = Core::EnvironmentPath(L"SystemRoot").value_or(std::filesystem::path{L"C:\\Windows"});

	return systemRoot / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe";
}

// A process writes its id followed by a line end, so a file that ends in one holds the whole id; empty if no id came
// before the deadline.
inline std::optional<std::uint32_t> WaitForProcessId(const std::filesystem::path& file, std::chrono::seconds deadline)
{
	constexpr std::chrono::milliseconds kPollInterval{50};
	const std::chrono::steady_clock::time_point giveUpAt = std::chrono::steady_clock::now() + deadline;

	while (std::chrono::steady_clock::now() < giveUpAt)
	{
		std::ifstream stream{file, std::ios::binary};
		const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
		const bool holdsWholeId = !text.empty() && text.back() == '\n';

		std::uint32_t processId = 0;
		const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), processId);

		if (holdsWholeId && parsed.ec == std::errc{} && processId != 0)
		{
			return processId;
		}

		std::this_thread::sleep_for(kPollInterval);
	}

	return std::nullopt;
}

// A process a test leaves running on purpose, ended when the test ends however it ends, and waited for, so that the
// directory it runs in can be removed after it.
class LeftBehindProcess
{
public:
	explicit LeftBehindProcess(std::uint32_t processId)
		: m_Process(::OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, processId))
	{
	}

	~LeftBehindProcess()
	{
		constexpr UINT kEndedByTheTest = 0;
		constexpr DWORD kTeardownWaitMilliseconds = 10000;

		if (m_Process)
		{
			::TerminateProcess(m_Process.Get(), kEndedByTheTest);
			::WaitForSingleObject(m_Process.Get(), kTeardownWaitMilliseconds);
		}
	}

	LeftBehindProcess(const LeftBehindProcess&) = delete;
	LeftBehindProcess& operator=(const LeftBehindProcess&) = delete;

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
	Core::UniqueHandle m_Process;
};
