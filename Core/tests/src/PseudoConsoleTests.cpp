#include <doctest/doctest.h>

#include "Core/Platform/PseudoConsole.h"

#include <array>
#include <chrono>
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
	std::array<char, 512> buffer{};

	// Drain the banner and prompt first, so the read below has nothing waiting and genuinely
	// blocks — otherwise this passes without proving anything.
	CHECK(ReadUntil(console, ">"));

	std::thread terminator([&console] {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		console.Terminate();
	});

	while (console.Read(buffer) != 0)
	{
	}

	terminator.join();
	CHECK_FALSE(console.IsRunning());
}
