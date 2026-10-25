#include "corepch.h"

#include "Core/Terminal/TerminalSession.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/Terminal/ControlCharacters.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace Core {

	namespace {
		// Above anything an ordinary command writes in a frame and far below what would stall one.
		// The reader waits when the queue is this full rather than drop bytes, which would cut
		// escape sequences into garbage, so the program feels the backpressure a real console applies.
		constexpr std::size_t kQueueLimitBytes = 1024 * 1024;
		constexpr std::size_t kMostBytesParsedPerFrame = 1024 * 1024;

		constexpr std::size_t kReadChunkBytes = 8192;
		constexpr std::size_t kFrontOfQueue = 0;

		// A read that returns nothing while the shell still runs has failed rather than blocked, so
		// retrying it at once would spin a core on the failure.
		constexpr std::chrono::milliseconds kPauseAfterEmptyRead{10};

		constexpr std::string_view kPasteStartMarker = "\x1B[200~";
		constexpr std::string_view kPasteEndMarker = "\x1B[201~";

		// Every ESC goes, not only the one starting an end marker: an embedded end marker would close
		// the paste early and run the rest as typed input, and dropping every ESC defuses that and
		// every other control sequence.
		void AppendWithoutEscapeCharacters(std::string& wireBytes, std::string_view pastedText)
		{
			for (const char character : pastedText)
			{
				if (character != kEscapeCharacter)
				{
					wireBytes.push_back(character);
				}
			}
		}
	}

	std::string BracketedPasteBytes(std::string_view text, bool shellTurnedBracketedPasteOn)
	{
		if (!shellTurnedBracketedPasteOn)
		{
			return std::string{text};
		}

		std::string wireBytes;
		wireBytes.reserve(kPasteStartMarker.size() + text.size() + kPasteEndMarker.size());
		wireBytes += kPasteStartMarker;
		AppendWithoutEscapeCharacters(wireBytes, text);
		wireBytes += kPasteEndMarker;

		return wireBytes;
	}

	TerminalSession::TerminalSession(Scope<PseudoConsole> console, std::string profileName,
									 std::uint16_t columns, std::uint16_t rows)
		: m_Console(std::move(console)), m_ProfileName(std::move(profileName)),
		  m_Screen(columns, rows), m_Parser(m_Screen)
	{
	}

	Result<Scope<TerminalSession>> TerminalSession::Start(
		const ShellProfile& profile, const std::filesystem::path& workingDirectory,
		std::uint16_t columns, std::uint16_t rows)
	{
		Result<Scope<PseudoConsole>> consoleResult =
			PseudoConsole::Start(profile.Path, profile.Arguments, workingDirectory, columns, rows);

		if (!consoleResult)
		{
			return consoleResult.GetError();
		}

		Scope<TerminalSession> session{new TerminalSession(
			std::move(consoleResult).GetValue(), profile.Name, columns, rows)};

		session->m_ReaderThread = std::thread([startedSession = session.get()] { startedSession->ReadLoop(); });

		return session;
	}

	TerminalSession::~TerminalSession()
	{
		{
			// Raised under the mutex, not merely atomically: the reader tests its wait predicate while
			// holding it, so a flag and notification landing between that test and the wait would be
			// missed, leaving the reader asleep and the join that waits for it waiting forever.
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Stopping.store(true, std::memory_order_relaxed);
		}

		// Terminate before joining: it is the only thing that releases a reader blocked in Read,
		// so joining first would wait forever for bytes that never come.
		if (m_Console)
		{
			m_Console->Terminate();
		}

		m_ReaderWake.notify_all();

		// ~PseudoConsole, which runs after this body, reads from the same pipe the reader is inside
		// until it returns; joining here keeps the two out of it at the same time.
		if (m_ReaderThread.joinable())
		{
			m_ReaderThread.join();
		}
	}

	void TerminalSession::ReadLoop()
	{
		std::array<char, kReadChunkBytes> readBuffer{};

		while (!m_Stopping.load(std::memory_order_relaxed))
		{
			const std::size_t bytesRead = m_Console->Read(readBuffer);

			if (bytesRead == 0)
			{
				const bool shellHasExited = !m_Console->IsRunning();

				if (shellHasExited)
				{
					break;
				}

				std::this_thread::sleep_for(kPauseAfterEmptyRead);
				continue;
			}

			std::unique_lock<std::mutex> lock(m_QueueMutex);

			m_ReaderWake.wait(lock, [this] {
				return m_QueuedOutput.size() < kQueueLimitBytes || m_Stopping.load(std::memory_order_relaxed);
			});

			if (m_Stopping.load(std::memory_order_relaxed))
			{
				break;
			}

			m_QueuedOutput.append(readBuffer.data(), bytesRead);

			// Without this wake a sleeping UI never pumps, the queue fills, and the shell blocks on
			// its own output.
			Redraw::Request();
		}

		{
			// WARNING: Lowered under the queue's mutex, which HasFinished reads both under: lowered outside it, a reader
			// could see the flag down and still find the last bytes not yet in the queue.
			std::lock_guard<std::mutex> lock(m_QueueMutex);
			m_Alive.store(false, std::memory_order_relaxed);
		}

		// So the tab of a shell that exited goes away without waiting for input.
		Redraw::Request();
	}

	bool TerminalSession::HasFinished() const
	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		return !m_Alive.load(std::memory_order_relaxed) && m_QueuedOutput.empty();
	}

	std::optional<std::uint32_t> TerminalSession::GetExitCode() const
	{
		if (!m_Console)
		{
			return std::nullopt;
		}

		return m_Console->GetExitCode();
	}

	void TerminalSession::ApplyQueuedOutput()
	{
		std::string outputThisFrame;

		{
			std::lock_guard<std::mutex> lock(m_QueueMutex);

			if (m_QueuedOutput.empty())
			{
				return;
			}

			const std::size_t bytesThisFrame = std::min(m_QueuedOutput.size(), kMostBytesParsedPerFrame);
			outputThisFrame.assign(m_QueuedOutput, kFrontOfQueue, bytesThisFrame);
			m_QueuedOutput.erase(kFrontOfQueue, bytesThisFrame);
		}

		m_ReaderWake.notify_all();

		const std::string replies = m_Parser.Consume(outputThisFrame);

		if (!replies.empty())
		{
			m_Console->Write(replies);
		}
	}

	void TerminalSession::Send(std::string_view bytes)
	{
		if (!IsAlive() || bytes.empty())
		{
			return;
		}

		m_Console->Write(bytes);
	}

	void TerminalSession::Paste(std::string_view text)
	{
		if (text.empty())
		{
			return;
		}

		// One write rather than three: the markers and the text they bracket must not be
		// interleaved with anything else the UI sends in the meantime.
		Send(BracketedPasteBytes(text, m_Screen.IsBracketedPaste()));
	}

	void TerminalSession::Resize(std::uint16_t columns, std::uint16_t rows)
	{
		const bool sizeIsUnchanged = columns == m_Screen.GetColumns() && rows == m_Screen.GetRows();

		if (sizeIsUnchanged)
		{
			return;
		}

		m_Screen.Resize(columns, rows);

		if (m_Console)
		{
			// A console refuses a resize once its shell has exited, and a window drag would log that
			// once per column; a refusal while the shell runs leaves it wrapping at the old width.
			const Result<void> resizeResult = m_Console->Resize(columns, rows);
			const bool refusedWhileShellRuns = !resizeResult && m_Console->IsRunning();

			if (refusedWhileShellRuns)
			{
				CORE_WARN("Cannot resize the terminal to {}x{}: {}", columns, rows, resizeResult.GetError().Message);
			}
		}
	}
}
