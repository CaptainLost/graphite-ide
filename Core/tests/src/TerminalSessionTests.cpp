#include <doctest/doctest.h>

#include "Core/Platform/ShellProfiles.h"
#include "Core/Term/TerminalSession.h"

#include <chrono>
#include <thread>

namespace {

	Core::ShellProfile CommandPrompt()
	{
		Core::ShellProfile profile;
		profile.Name = "Command Prompt";
		profile.Path = "cmd.exe";
		return profile;
	}

	std::u32string ScreenText(const Core::TerminalScreen& screen)
	{
		std::u32string text;

		for (std::uint16_t row = 0; row < screen.GetRows(); ++row)
		{
			for (std::uint16_t column = 0; column < screen.GetColumns(); ++column)
			{
				text.push_back(screen.At(row, column).Code);
			}
		}

		return text;
	}

	// Pumping is the UI thread's job, so a test stands in for the frame loop.
	bool PumpUntil(Core::TerminalSession& session, std::u32string_view wanted)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (std::chrono::steady_clock::now() < deadline)
		{
			session.Pump();

			if (ScreenText(session.GetScreen()).find(wanted) != std::u32string::npos)
			{
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		return false;
	}
}

TEST_CASE("TerminalSession puts a command's output on the screen")
{
	auto session = Core::TerminalSession::Start(CommandPrompt(), std::filesystem::current_path(),
												80, 25);

	REQUIRE(session);
	REQUIRE(session.GetValue()->IsAlive());

	session.GetValue()->Send("echo session-works\r\n");

	CHECK(PumpUntil(*session.GetValue(), U"session-works"));
}

TEST_CASE("TerminalSession stops being alive after the shell exits")
{
	auto session = Core::TerminalSession::Start(CommandPrompt(), std::filesystem::current_path(),
												80, 25);

	REQUIRE(session);

	session.GetValue()->Send("exit\r\n");

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

	while (session.GetValue()->IsAlive() && std::chrono::steady_clock::now() < deadline)
	{
		session.GetValue()->Pump();
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	CHECK_FALSE(session.GetValue()->IsAlive());
}

TEST_CASE("TerminalSession resizes its screen with its shell")
{
	auto session = Core::TerminalSession::Start(CommandPrompt(), std::filesystem::current_path(),
												80, 25);

	REQUIRE(session);

	session.GetValue()->Resize(100, 30);

	CHECK(session.GetValue()->GetScreen().GetColumns() == 100);
	CHECK(session.GetValue()->GetScreen().GetRows() == 30);
}

TEST_CASE("TerminalSession destroying a live session returns promptly")
{
	// The shutdown order is the point: terminate, then join. Getting it wrong hangs here
	// rather than failing, so a deadline turns a hang into a failure.
	const auto started = std::chrono::steady_clock::now();

	{
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);
		REQUIRE(session);
	}

	CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
}
