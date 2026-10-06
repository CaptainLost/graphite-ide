#include <doctest/doctest.h>

#include "Core/Platform/PseudoConsole.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

	using StartedConsole = Core::Result<Core::Scope<Core::PseudoConsole>>;

	constexpr std::uint16_t kColumns = 80;
	constexpr std::uint16_t kRows = 25;
	constexpr std::uint16_t kResizedColumns = 100;
	constexpr std::uint16_t kResizedRows = 30;

	constexpr std::size_t kReadBufferBytes = 512;

	constexpr std::chrono::seconds kPatience{10};
	constexpr std::chrono::milliseconds kDelayBeforeTerminating{200};
	constexpr std::chrono::milliseconds kReleasePollInterval{10};

	// A live shell never reports end-of-output, so a test cannot read to exhaustion.
	bool ReadUntilTextArrives(Core::PseudoConsole& console, std::string_view wantedText)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kPatience;
		std::string receivedText;
		std::array<char, kReadBufferBytes> buffer{};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const std::size_t bytesRead = console.Read(buffer);

			if (bytesRead == 0)
			{
				break;
			}

			receivedText.append(buffer.data(), bytesRead);

			if (receivedText.find(wantedText) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	void ReadUntilReadReturnsZero(Core::PseudoConsole& console)
	{
		std::array<char, kReadBufferBytes> buffer{};
		std::size_t bytesRead = console.Read(buffer);

		while (bytesRead != 0)
		{
			bytesRead = console.Read(buffer);
		}
	}
}

TEST_CASE("PseudoConsole runs a command and reads its output")
{
	const std::vector<std::string> echoHello{"/c", "echo hello"};
	StartedConsole started = Core::PseudoConsole::Start("cmd.exe", echoHello,
														std::filesystem::current_path(), kColumns, kRows);

	REQUIRE(started);
	CHECK(ReadUntilTextArrives(*started.GetValue(), "hello"));
}

TEST_CASE("PseudoConsole writes to the shell's input")
{
	const std::vector<std::string> noArguments;
	StartedConsole started = Core::PseudoConsole::Start("cmd.exe", noArguments, std::filesystem::current_path(),
														kColumns, kRows);

	REQUIRE(started);
	REQUIRE(started.GetValue()->Write("echo written\r\n"));
	CHECK(ReadUntilTextArrives(*started.GetValue(), "written"));
}

TEST_CASE("PseudoConsole resizes a live shell")
{
	const std::vector<std::string> noArguments;
	StartedConsole started = Core::PseudoConsole::Start("cmd.exe", noArguments, std::filesystem::current_path(),
														kColumns, kRows);

	REQUIRE(started);
	CHECK(started.GetValue()->Resize(kResizedColumns, kResizedRows));
}

TEST_CASE("PseudoConsole reports a program that does not exist")
{
	const std::vector<std::string> noArguments;
	StartedConsole started = Core::PseudoConsole::Start("no-such-program-4b71.exe", noArguments,
														std::filesystem::current_path(), kColumns, kRows);

	REQUIRE_FALSE(started);
	CHECK(started.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("PseudoConsole terminating the shell releases a thread blocked in Read")
{
	const std::vector<std::string> noArguments;
	StartedConsole started = Core::PseudoConsole::Start("cmd.exe", noArguments, std::filesystem::current_path(),
														kColumns, kRows);

	REQUIRE(started);

	Core::PseudoConsole& console = *started.GetValue();

	// The banner and prompt are drained first so that the read below finds nothing waiting and
	// genuinely blocks; otherwise the case passes without proving anything.
	CHECK(ReadUntilTextArrives(console, ">"));

	std::thread terminator([&console] {
		std::this_thread::sleep_for(kDelayBeforeTerminating);
		console.Terminate();
	});

	// A single Read that never returns would hang the whole suite, so the reader runs on its own
	// thread and this one waits with a deadline. The flag is on the heap and the buffer on the
	// reader's stack, because on failure the reader outlives this scope.
	auto released = std::make_shared<std::atomic<bool>>(false);

	std::thread reader([&console, released] {
		ReadUntilReadReturnsZero(console);
		released->store(true);
	});

	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kPatience;

	while (!released->load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(kReleasePollInterval);
	}

	CHECK(released->load());
	terminator.join();

	if (released->load())
	{
		reader.join();
	}
	else
	{
		// The reader is still parked inside Read on a handle the console owns, and destroying the
		// console under it would turn a failed case into a crash on the way out, so it is leaked.
		reader.detach();
		(void)started.GetValue().release();
		return;
	}

	CHECK_FALSE(console.IsRunning());
}
