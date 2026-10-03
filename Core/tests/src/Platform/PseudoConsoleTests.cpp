#include <doctest/doctest.h>

#include "Core/Platform/PseudoConsole.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

	// A pseudo-console never reports end-of-output the way a pipe does while the shell is
	// alive, so a test cannot read to exhaustion. It reads until the text it is waiting for
	// has arrived, or until it runs out of patience.
	bool ReadUntil(Core::PseudoConsole& console, std::string_view wanted)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		std::string text;
		std::array<char, 512> buffer{};

		while (std::chrono::steady_clock::now() < deadline)
		{
			const std::size_t read = console.Read(buffer);

			if (read == 0)
			{
				break;
			}

			text.append(buffer.data(), read);

			if (text.find(wanted) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("PseudoConsole runs a command and reads its output")
{
	auto console = Core::PseudoConsole::Start("cmd.exe", {"/c", "echo hello"},
											  std::filesystem::current_path(), 80, 25);

	REQUIRE(console);
	CHECK(ReadUntil(*console.GetValue(), "hello"));
}

TEST_CASE("PseudoConsole writes to the shell's input")
{
	auto console = Core::PseudoConsole::Start("cmd.exe", {}, std::filesystem::current_path(),
											  80, 25);

	REQUIRE(console);
	REQUIRE(console.GetValue()->Write("echo written\r\n"));
	CHECK(ReadUntil(*console.GetValue(), "written"));
}

TEST_CASE("PseudoConsole resizes a live shell")
{
	auto console = Core::PseudoConsole::Start("cmd.exe", {}, std::filesystem::current_path(),
											  80, 25);

	REQUIRE(console);
	CHECK(console.GetValue()->Resize(100, 30));
}

TEST_CASE("PseudoConsole reports a program that does not exist")
{
	auto console = Core::PseudoConsole::Start("no-such-program-4b71.exe", {},
											  std::filesystem::current_path(), 80, 25);

	REQUIRE_FALSE(console);
	CHECK(console.GetError().Code == Core::ErrorCode::NotFound);
}

TEST_CASE("PseudoConsole terminating the shell releases a thread blocked in Read")
{
	auto started = Core::PseudoConsole::Start("cmd.exe", {}, std::filesystem::current_path(),
											  80, 25);

	REQUIRE(started);

	Core::PseudoConsole& console = *started.GetValue();

	// Drain the banner and prompt first, so the read below has nothing waiting and genuinely
	// blocks — otherwise this passes without proving anything.
	CHECK(ReadUntil(console, ">"));

	std::thread terminator([&console] {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		console.Terminate();
	});

	// The claim under test is that Read returns zero once the shell is killed, so the loop
	// itself has to be the thing that is bounded. A deadline checked between iterations would
	// not do it: the defect this guards against is a single Read that never returns, and it
	// took out the whole suite by hanging rather than failing. Running the loop on its own
	// thread and giving the main thread a deadline turns that into one red case in seconds.
	// The buffer and the flag live with the reader — heap-allocated flag, thread-local buffer —
	// because on failure that thread is still running when this one leaves the scope.
	auto released = std::make_shared<std::atomic<bool>>(false);

	std::thread reader([&console, released] {
		std::array<char, 512> buffer{};

		while (console.Read(buffer) != 0)
		{
		}

		released->store(true);
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	while (!released->load() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(released->load());
	terminator.join();

	if (released->load())
	{
		reader.join();
	}
	else
	{
		// The reader is still parked inside Read, on a handle this object owns. Destroying the
		// console would pull that handle out from under a live thread and turn a failed case
		// into a crash on the way out, so the failing run deliberately leaks it instead.
		reader.detach();
		(void)started.GetValue().release();
		return;
	}

	CHECK_FALSE(console.IsRunning());
}
