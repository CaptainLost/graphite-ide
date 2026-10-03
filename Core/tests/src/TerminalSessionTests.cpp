#include <doctest/doctest.h>

#include "Core/Platform/ShellProfiles.h"
#include "Core/Term/TerminalScreen.h"
#include "Core/Term/TerminalSession.h"
#include "Core/Term/VtParser.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

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

	// Runs work that owns a session — creation, use, and above all destruction — on a thread of
	// its own, and reports whether it finished inside the limit.
	//
	// Every session in this file is destroyed somewhere a deadline can see it, because a
	// shutdown bug in TerminalSession shows up as a join that never returns. Run inline, that
	// stops the whole binary: a scope's closing brace cannot be timed from inside the scope, and
	// a hung suite has to be killed from outside, which destroys the entire run's results rather
	// than reddening one case.
	//
	// The thread is deliberately detached and the promise deliberately shared: when the deadline
	// expires, the stuck work is abandoned — leaking the session and its thread — so the case can
	// fail and the cases after it can still run. Nothing here touches a doctest macro off the
	// main thread; results come back through the shared state and are asserted by the caller.
	bool FinishesWithin(std::chrono::milliseconds limit, std::function<void()> work)
	{
		auto finished = std::make_shared<std::promise<void>>();
		std::future<void> done = finished->get_future();

		std::thread([finished, work = std::move(work)]() mutable {
			work();
			finished->set_value();
		}).detach();

		return done.wait_for(limit) == std::future_status::ready;
	}

	// Deadlines are generous on purpose: they are there to convert a hang into a failure, not to
	// measure how fast a shell starts, so no correct implementation can miss them by being busy.
	constexpr std::chrono::seconds ShutdownLimit{5};
	constexpr std::chrono::seconds SessionLimit{40};
}

TEST_CASE("TerminalSession puts a command's output on the screen")
{
	struct Outcome
	{
		std::atomic<bool> Started{false};
		std::atomic<bool> Alive{false};
		std::atomic<bool> Echoed{false};
	};

	auto outcome = std::make_shared<Outcome>();

	const bool finished = FinishesWithin(SessionLimit, [outcome] {
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);

		if (!session)
		{
			return;
		}

		outcome->Started.store(true);
		outcome->Alive.store(session.GetValue()->IsAlive());

		session.GetValue()->Send("echo session-works\r\n");

		outcome->Echoed.store(PumpUntil(*session.GetValue(), U"session-works"));
	});

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK(outcome->Alive.load());
	CHECK(outcome->Echoed.load());
}

TEST_CASE("TerminalSession stops being alive after the shell exits")
{
	struct Outcome
	{
		std::atomic<bool> Started{false};
		std::atomic<bool> Alive{true};
	};

	auto outcome = std::make_shared<Outcome>();

	const bool finished = FinishesWithin(SessionLimit, [outcome] {
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);

		if (!session)
		{
			return;
		}

		outcome->Started.store(true);
		session.GetValue()->Send("exit\r\n");

		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (session.GetValue()->IsAlive() && std::chrono::steady_clock::now() < deadline)
		{
			session.GetValue()->Pump();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		outcome->Alive.store(session.GetValue()->IsAlive());
	});

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK_FALSE(outcome->Alive.load());
}

TEST_CASE("TerminalSession resizes its screen with its shell")
{
	struct Outcome
	{
		std::atomic<bool> Started{false};
		std::atomic<std::uint16_t> Columns{0};
		std::atomic<std::uint16_t> Rows{0};
	};

	auto outcome = std::make_shared<Outcome>();

	const bool finished = FinishesWithin(SessionLimit, [outcome] {
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);

		if (!session)
		{
			return;
		}

		outcome->Started.store(true);
		session.GetValue()->Resize(100, 30);

		outcome->Columns.store(session.GetValue()->GetScreen().GetColumns());
		outcome->Rows.store(session.GetValue()->GetScreen().GetRows());
	});

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK(outcome->Columns.load() == 100);
	CHECK(outcome->Rows.load() == 30);
}

TEST_CASE("TerminalSession destroying a live session returns promptly")
{
	// The shutdown order is the point: terminate, then join. Getting it wrong hangs in the
	// destructor, which is why the destructor runs on a thread the deadline below can observe.
	auto started = std::make_shared<std::atomic<bool>>(false);

	const bool finished = FinishesWithin(ShutdownLimit, [started] {
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);
		started->store(static_cast<bool>(session));
	});

	CHECK(finished);
	CHECK(started->load());
}

TEST_CASE("TerminalSession destroying a session with a full queue returns promptly")
{
	// The reader blocks once the queue reaches its cap, waiting on the condition variable for
	// the UI thread to drain it. Nothing here ever calls Pump, so that drain never comes and the
	// only thing that can release the reader is the destructor's own flag and notification. If
	// that wakeup is lost, the join never returns — hence the deadline, and hence the thread.
	const std::filesystem::path filler =
		std::filesystem::temp_directory_path() / "graphite-terminal-queue-filler.txt";

	{
		// Several times the queue limit, so the queue is full long before the shell runs out of
		// bytes to write. Its content is irrelevant: the queue holds raw bytes and is never
		// parsed in this case.
		const std::string line(1023, 'a');
		std::ofstream out(filler, std::ios::binary | std::ios::trunc);

		for (int written = 0; written < 4096; ++written)
		{
			out << line << '\n';
		}
	}

	auto started = std::make_shared<std::atomic<bool>>(false);

	const bool finished = FinishesWithin(ShutdownLimit + std::chrono::seconds(5), [started, filler] {
		auto session = Core::TerminalSession::Start(CommandPrompt(),
													std::filesystem::current_path(), 80, 25);

		if (!session)
		{
			return;
		}

		started->store(true);
		session.GetValue()->Send("type \"" + filler.string() + "\"\r\n");

		// Long enough for the shell to pour four megabytes into a queue capped at one, with no
		// Pump to relieve it, which leaves the reader parked in its wait.
		std::this_thread::sleep_for(std::chrono::seconds(2));
	});

	CHECK(finished);
	CHECK(started->load());

	std::error_code status;
	std::filesystem::remove(filler, status);
}

TEST_CASE("BracketedPasteBytes wraps a paste only when the shell asked for the markers")
{
	// The screen's flag is driven the way the shell drives it — through the parser — rather than
	// set by hand, so this covers the sequence that turns the mode on as well as the choice it
	// governs. No shell is spawned: the decision is all that is under test.
	Core::TerminalScreen screen(80, 25);
	Core::VtParser parser(screen);

	REQUIRE_FALSE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "one\ntwo");

	parser.Consume("\x1B[?2004h");

	REQUIRE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "\x1B[200~one\ntwo\x1B[201~");

	parser.Consume("\x1B[?2004l");

	REQUIRE_FALSE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "one\ntwo");
}

TEST_CASE("BracketedPasteBytes strips ESC so pasted text cannot close the paste early")
{
	const std::string bytes = Core::BracketedPasteBytes("a\x1B[201~rm -rf x\x1B[200~b", true);

	CHECK(bytes == "\x1B[200~a[201~rm -rf x[200~b\x1B[201~");

	std::size_t closers = 0;
	for (std::size_t at = bytes.find("\x1B[201~"); at != std::string::npos;
		 at = bytes.find("\x1B[201~", at + 1))
	{
		++closers;
	}
	CHECK(closers == 1);
	CHECK(bytes.rfind("\x1B[201~") == bytes.size() - 6);
}
