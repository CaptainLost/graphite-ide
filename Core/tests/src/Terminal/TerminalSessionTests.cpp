#include <doctest/doctest.h>

#include "Core/Platform/ShellProfiles.h"
#include "Core/Terminal/TerminalScreen.h"
#include "Core/Terminal/TerminalSession.h"
#include "Core/Terminal/VtParser.h"

#include "TerminalFixtures.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

	constexpr std::uint16_t kInitialColumns = 80;
	constexpr std::uint16_t kInitialRows = 25;
	constexpr std::uint16_t kResizedColumns = 100;
	constexpr std::uint16_t kResizedRows = 30;

	constexpr std::chrono::seconds kShellResponseLimit{10};
	constexpr std::chrono::milliseconds kFrameInterval{20};

	// Generous on purpose: they turn a hang into a failure and do not measure how fast a shell
	// starts, so no correct implementation can miss them by being busy.
	constexpr std::chrono::seconds kShutdownLimit{5};
	constexpr std::chrono::seconds kSessionLimit{40};

	// TerminalSession's queue holds one mebibyte and its reader parks only once this file overflows
	// it, so raising that limit means raising this count.
	constexpr std::size_t kFillerLineLengthBeforeNewline = 1023;
	constexpr int kFillerLineCountForFourTimesTheQueueLimit = 4096;
	static_assert((kFillerLineLengthBeforeNewline + 1) * kFillerLineCountForFourTimesTheQueueLimit == std::size_t{4} * 1024 * 1024);
	constexpr char kAnyFillerCharacter = 'a';
	constexpr std::chrono::seconds kSessionStartAllowance{3};
	constexpr std::chrono::seconds kTimeForTheShellToFillTheQueue{2};

	constexpr std::string_view kShellTurnsBracketedPasteOn = "\x1B[?2004h";
	constexpr std::string_view kShellTurnsBracketedPasteOff = "\x1B[?2004l";
	constexpr std::string_view kPasteEndMarker = "\x1B[201~";

	Core::ShellProfile CommandPrompt()
	{
		return Core::ShellProfile{.Name = "Command Prompt", .Path = "cmd.exe"};
	}

	Core::Result<Core::Scope<Core::TerminalSession>> StartCommandPrompt()
	{
		return Core::TerminalSession::Start(CommandPrompt(), std::filesystem::current_path(), kInitialColumns, kInitialRows);
	}

	std::u32string ScreenText(const Core::TerminalScreen& screen)
	{
		std::u32string text;

		for (std::uint16_t row = 0; row < screen.GetRows(); ++row)
		{
			text += ScreenRowText(screen, row);
		}

		return text;
	}

	bool ApplyQueuedOutputUntilScreenShows(Core::TerminalSession& session, std::u32string_view expectedText)
	{
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kShellResponseLimit;

		while (std::chrono::steady_clock::now() < deadline)
		{
			session.ApplyQueuedOutput();

			const bool screenShowsExpectedText = ScreenText(session.GetScreen()).find(expectedText) != std::u32string::npos;

			if (screenShowsExpectedText)
			{
				return true;
			}

			std::this_thread::sleep_for(kFrameInterval);
		}

		return false;
	}

	std::size_t CountOccurrences(const std::string& text, std::string_view pattern)
	{
		std::size_t occurrenceCount = 0;

		for (std::size_t matchPosition = text.find(pattern); matchPosition != std::string::npos;
			 matchPosition = text.find(pattern, matchPosition + 1))
		{
			++occurrenceCount;
		}

		return occurrenceCount;
	}

	// A shutdown bug shows up as a join that never returns, which run inline would hang the whole
	// suite; on a detached thread the stuck work is abandoned and leaked, so this case fails and the
	// cases after it still run, and the promise is shared because the abandoned thread may still set
	// it after this function has returned. Doctest macros must not be called off the main thread, so
	// results come back through shared state for the caller to assert.
	bool FinishesOnItsOwnThreadWithin(std::chrono::milliseconds limit, std::function<void()> work)
	{
		auto completion = std::make_shared<std::promise<void>>();
		std::future<void> completionReport = completion->get_future();

		std::thread([completion, work = std::move(work)]() mutable {
			work();
			completion->set_value();
		}).detach();

		const std::future_status waitStatus = completionReport.wait_for(limit);
		return waitStatus == std::future_status::ready;
	}

	struct EchoOutcome
	{
		std::atomic<bool> Started{false};
		std::atomic<bool> Alive{false};
		std::atomic<bool> Echoed{false};
	};

	void StartAndEcho(EchoOutcome& outcome)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();

		if (!sessionResult)
		{
			return;
		}

		Core::TerminalSession& session = *sessionResult.GetValue();

		outcome.Started.store(true);
		outcome.Alive.store(session.IsAlive());

		session.Send("echo session-works\r\n");

		outcome.Echoed.store(ApplyQueuedOutputUntilScreenShows(session, U"session-works"));
	}

	struct ExitOutcome
	{
		std::atomic<bool> Started{false};
		std::atomic<bool> Alive{true};
	};

	void StartAndExitTheShell(ExitOutcome& outcome)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();

		if (!sessionResult)
		{
			return;
		}

		Core::TerminalSession& session = *sessionResult.GetValue();

		outcome.Started.store(true);
		session.Send("exit\r\n");

		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kShellResponseLimit;

		while (session.IsAlive() && std::chrono::steady_clock::now() < deadline)
		{
			session.ApplyQueuedOutput();
			std::this_thread::sleep_for(kFrameInterval);
		}

		outcome.Alive.store(session.IsAlive());
	}

	constexpr std::uint32_t kCommandExitCode = 3;

	struct FinishOutcome
	{
		std::atomic<bool> Started{false};
		std::atomic<bool> Finished{false};
		std::atomic<bool> OutputShownWhenFinished{false};
		std::atomic<bool> ExitCodeKnown{false};
		std::atomic<std::uint32_t> ExitCode{0};
	};

	void StartCommandAndWaitUntilFinished(FinishOutcome& outcome)
	{
		const Core::ShellProfile echoThenExit{.Name = "Command",
											  .Path = "cmd.exe",
											  .Arguments = {"/c", "echo command-output& exit 3"}};
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult =
			Core::TerminalSession::Start(echoThenExit, std::filesystem::current_path(), kInitialColumns, kInitialRows);

		if (!sessionResult)
		{
			return;
		}

		Core::TerminalSession& session = *sessionResult.GetValue();
		outcome.Started.store(true);

		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kShellResponseLimit;

		while (!session.HasFinished() && std::chrono::steady_clock::now() < deadline)
		{
			session.ApplyQueuedOutput();
			std::this_thread::sleep_for(kFrameInterval);
		}

		outcome.Finished.store(session.HasFinished());
		outcome.OutputShownWhenFinished.store(ScreenText(session.GetScreen()).find(U"command-output") != std::u32string::npos);

		const std::optional<std::uint32_t> exitCode = session.GetExitCode();
		outcome.ExitCodeKnown.store(exitCode.has_value());
		outcome.ExitCode.store(exitCode.value_or(0));
	}

	struct ResizeOutcome
	{
		std::atomic<bool> Started{false};
		std::atomic<std::uint16_t> Columns{0};
		std::atomic<std::uint16_t> Rows{0};
	};

	void StartAndResize(ResizeOutcome& outcome)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();

		if (!sessionResult)
		{
			return;
		}

		Core::TerminalSession& session = *sessionResult.GetValue();

		outcome.Started.store(true);
		session.Resize(kResizedColumns, kResizedRows);

		outcome.Columns.store(session.GetScreen().GetColumns());
		outcome.Rows.store(session.GetScreen().GetRows());
	}

	void StartAndDestroy(std::atomic<bool>& started)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();
		started.store(static_cast<bool>(sessionResult));
	}

	void StartAndFillTheQueue(const std::filesystem::path& fillerFile, std::atomic<bool>& started)
	{
		Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();

		if (!sessionResult)
		{
			return;
		}

		started.store(true);
		sessionResult.GetValue()->Send("type \"" + fillerFile.string() + "\"\r\n");

		std::this_thread::sleep_for(kTimeForTheShellToFillTheQueue);
	}
}

TEST_CASE("TerminalSession puts a command's output on the screen")
{
	auto outcome = std::make_shared<EchoOutcome>();

	const bool finished = FinishesOnItsOwnThreadWithin(kSessionLimit, [outcome] { StartAndEcho(*outcome); });

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK(outcome->Alive.load());
	CHECK(outcome->Echoed.load());
}

TEST_CASE("TerminalSession stops being alive after the shell exits")
{
	auto outcome = std::make_shared<ExitOutcome>();

	const bool finished = FinishesOnItsOwnThreadWithin(kSessionLimit, [outcome] { StartAndExitTheShell(*outcome); });

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK_FALSE(outcome->Alive.load());
}

TEST_CASE("TerminalSession finishes once its program exited and its output is on the screen, and knows the exit code")
{
	auto outcome = std::make_shared<FinishOutcome>();

	const bool finished =
		FinishesOnItsOwnThreadWithin(kSessionLimit, [outcome] { StartCommandAndWaitUntilFinished(*outcome); });

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK(outcome->Finished.load());
	CHECK(outcome->OutputShownWhenFinished.load());
	REQUIRE(outcome->ExitCodeKnown.load());
	CHECK(outcome->ExitCode.load() == kCommandExitCode);
}

TEST_CASE("TerminalSession is not finished while its shell runs")
{
	Core::Result<Core::Scope<Core::TerminalSession>> sessionResult = StartCommandPrompt();

	REQUIRE(sessionResult);
	CHECK_FALSE(sessionResult.GetValue()->HasFinished());
	CHECK_FALSE(sessionResult.GetValue()->GetExitCode().has_value());
}

TEST_CASE("TerminalSession resizes its screen with its shell")
{
	auto outcome = std::make_shared<ResizeOutcome>();

	const bool finished = FinishesOnItsOwnThreadWithin(kSessionLimit, [outcome] { StartAndResize(*outcome); });

	REQUIRE(finished);
	REQUIRE(outcome->Started.load());
	CHECK(outcome->Columns.load() == kResizedColumns);
	CHECK(outcome->Rows.load() == kResizedRows);
}

TEST_CASE("TerminalSession destroying a live session returns promptly because it terminates the shell before joining the reader")
{
	auto started = std::make_shared<std::atomic<bool>>(false);

	const bool finished = FinishesOnItsOwnThreadWithin(kShutdownLimit, [started] { StartAndDestroy(*started); });

	CHECK(finished);
	CHECK(started->load());
}

TEST_CASE("TerminalSession destroying a session whose reader waits on a full queue that nothing pumps returns promptly")
{
	const std::filesystem::path fillerFile =
		std::filesystem::temp_directory_path() / "graphite-terminal-queue-filler.txt";

	{
		const std::string fillerLine(kFillerLineLengthBeforeNewline, kAnyFillerCharacter);
		std::ofstream fillerStream(fillerFile, std::ios::binary | std::ios::trunc);

		for (int linesWritten = 0; linesWritten < kFillerLineCountForFourTimesTheQueueLimit; ++linesWritten)
		{
			fillerStream << fillerLine << '\n';
		}
	}

	auto started = std::make_shared<std::atomic<bool>>(false);

	const bool finished = FinishesOnItsOwnThreadWithin(kShutdownLimit + kTimeForTheShellToFillTheQueue + kSessionStartAllowance,
													   [started, fillerFile] { StartAndFillTheQueue(fillerFile, *started); });

	CHECK(finished);
	CHECK(started->load());

	std::error_code removeError;
	std::filesystem::remove(fillerFile, removeError);
}

TEST_CASE("BracketedPasteBytes wraps a paste only when the shell asked for the markers")
{
	Core::TerminalScreen screen(kInitialColumns, kInitialRows);
	Core::VtParser parser(screen);

	REQUIRE_FALSE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "one\ntwo");

	parser.Consume(kShellTurnsBracketedPasteOn);

	REQUIRE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "\x1B[200~one\ntwo\x1B[201~");

	parser.Consume(kShellTurnsBracketedPasteOff);

	REQUIRE_FALSE(screen.IsBracketedPaste());
	CHECK(Core::BracketedPasteBytes("one\ntwo", screen.IsBracketedPaste()) == "one\ntwo");
}

TEST_CASE("BracketedPasteBytes strips ESC so pasted text cannot close the paste early")
{
	const bool shellTurnedBracketedPasteOn = true;
	const std::string bytes = Core::BracketedPasteBytes("a\x1B[201~rm -rf x\x1B[200~b", shellTurnedBracketedPasteOn);

	CHECK(bytes == "\x1B[200~a[201~rm -rf x[200~b\x1B[201~");

	const std::size_t pasteEndMarkerCount = CountOccurrences(bytes, kPasteEndMarker);
	CHECK(pasteEndMarkerCount == 1);
	CHECK(bytes.rfind(kPasteEndMarker) == bytes.size() - kPasteEndMarker.size());
}
