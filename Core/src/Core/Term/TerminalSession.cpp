#include "corepch.h"

#include "Core/Term/TerminalSession.h"

#include "Core/App/Redraw.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace Core {

	namespace {
		// Above anything an ordinary command produces in a frame, and far below what would
		// stall one. The reader waits when the queue is this full, which pushes back through
		// the pseudo-console to the program — the same backpressure a real console applies.
		constexpr std::size_t QueueLimit = 1024 * 1024;
		constexpr std::size_t PerFrameLimit = 1024 * 1024;

		// How long the reader pauses after a read that produced nothing while the shell is
		// still running. Such a read failed rather than blocked, so retrying it straight away
		// would spin a core on the failure.
		constexpr std::chrono::milliseconds RetryPause{10};
	}

	std::string BracketedPasteBytes(std::string_view text, bool bracketedPasteEnabled)
	{
		if (!bracketedPasteEnabled)
		{
			return std::string{text};
		}

		std::string bytes;
		bytes.reserve(text.size() + 12);
		bytes += "\x1B[200~";
		// Pasted text must not contain ESC: an embedded ESC[201~ would end the paste early and
		// the rest would run as typed input. Dropping every ESC defuses all control sequences.
		std::ranges::copy_if(text, std::back_inserter(bytes), [](char c) { return c != '\x1B'; });
		bytes += "\x1B[201~";

		return bytes;
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
		Result<Scope<PseudoConsole>> console =
			PseudoConsole::Start(profile.Path, profile.Arguments, workingDirectory, columns, rows);

		if (!console)
		{
			return console.GetError();
		}

		Scope<TerminalSession> session{new TerminalSession(
			std::move(console).GetValue(), profile.Name, columns, rows)};

		session->m_Reader = std::thread([raw = session.get()] { raw->ReadLoop(); });

		return session;
	}

	TerminalSession::~TerminalSession()
	{
		{
			// Under the mutex, not merely atomic: the reader evaluates its wait predicate while
			// holding this, so a flag raised outside it could be set — and the notification
			// below delivered — in the window between that evaluation and the wait itself. The
			// reader would then sleep on a notification that has already been sent, and the
			// join below would never return.
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stopping.store(true, std::memory_order_relaxed);
		}

		// Terminate first: it releases a reader blocked in Read, which is the only way the
		// join below can complete. Joining first would wait for a thread that is waiting for
		// bytes that will never come.
		if (m_Console)
		{
			m_Console->Terminate();
		}

		m_Drained.notify_all();

		// The reader is inside the pipe until this returns, and ~PseudoConsole — which runs
		// when m_Console is destroyed, after this body — reads from that same pipe on its way
		// out. Joining here is what keeps the two out of it at the same time.
		if (m_Reader.joinable())
		{
			m_Reader.join();
		}
	}

	void TerminalSession::ReadLoop()
	{
		std::array<char, 8192> buffer{};

		while (!m_Stopping.load(std::memory_order_relaxed))
		{
			const std::size_t read = m_Console->Read(buffer);

			if (read == 0)
			{
				// Zero means "no bytes from this call", not end of stream: a read that could not
				// set up its wait, or that failed outright, reports zero as well. Ending the
				// session on one of those would close a terminal whose shell is still there, so
				// the shell itself is asked.
				if (!m_Console->IsRunning())
				{
					break;
				}

				std::this_thread::sleep_for(RetryPause);
				continue;
			}

			std::unique_lock<std::mutex> lock(m_Mutex);

			m_Drained.wait(lock, [this] {
				return m_Incoming.size() < QueueLimit || m_Stopping.load(std::memory_order_relaxed);
			});

			if (m_Stopping.load(std::memory_order_relaxed))
			{
				break;
			}

			m_Incoming.append(buffer.data(), read);

			// Without the wake a sleeping UI never pumps, the queue fills, and the shell
			// blocks on its own output.
			Redraw::Request();
		}

		m_Alive.store(false, std::memory_order_relaxed);

		// So the tab of a shell that exited goes away without waiting for input.
		Redraw::Request();
	}

	void TerminalSession::Pump()
	{
		std::string batch;

		{
			std::lock_guard<std::mutex> lock(m_Mutex);

			if (m_Incoming.empty())
			{
				return;
			}

			const std::size_t take = std::min(m_Incoming.size(), PerFrameLimit);
			batch.assign(m_Incoming, 0, take);
			m_Incoming.erase(0, take);
		}

		m_Drained.notify_all();

		const std::string replies = m_Parser.Consume(batch);

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
		if (columns == m_Screen.GetColumns() && rows == m_Screen.GetRows())
		{
			return;
		}

		m_Screen.Resize(columns, rows);

		if (m_Console)
		{
			(void)m_Console->Resize(columns, rows);
		}
	}
}
