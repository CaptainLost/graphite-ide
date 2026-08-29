#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/PseudoConsole.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Term/TerminalScreen.h"
#include "Core/Term/VtParser.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Core {

	// The bytes a paste puts on the wire: the text wrapped in the bracketed-paste markers when
	// the shell turned that mode on, and the text alone — indistinguishable from typing — when
	// it did not.
	//
	// Free rather than a private step inside Paste so that the decision can be tested against a
	// screen alone, with no shell to spawn and no wire to watch.
	std::string BracketedPasteBytes(std::string_view text, bool bracketedPasteEnabled);

	// One shell, its screen, and the thread that reads from it.
	//
	// The reader thread does not parse: it pushes raw bytes into a bounded queue, and the UI
	// thread drains that queue through the parser once a frame in Pump. The screen is
	// therefore touched by one thread only and needs no lock — which matters because the
	// alternative, a lock around the screen, would be held for the whole of drawing.
	//
	// The queue is bounded so that a program pouring out bytes faster than a frame can consume
	// them cannot grow it without limit. Dropping bytes is not an option — a truncated escape
	// sequence puts garbage on the screen — so the reader waits instead, the pseudo-console
	// fills, and the program feels the same backpressure a real console applies to it.
	class TerminalSession : NonCopyable
	{
	public:
		static Result<Scope<TerminalSession>> Start(const ShellProfile& profile,
													const std::filesystem::path& workingDirectory,
													std::uint16_t columns, std::uint16_t rows);

		~TerminalSession();

		// Drains what the shell has written since the last frame and applies it to the screen.
		// UI thread only.
		void Pump();

		void Send(std::string_view bytes);

		// Wraps the text in the shell's paste markers when it asked for them, so a multi-line
		// paste is not executed line by line as it arrives.
		void Paste(std::string_view text);

		void Resize(std::uint16_t columns, std::uint16_t rows);

		bool IsAlive() const { return m_Alive.load(std::memory_order_relaxed); }

		TerminalScreen& GetScreen() { return m_Screen; }
		const TerminalScreen& GetScreen() const { return m_Screen; }

		const std::string& GetProfileName() const { return m_ProfileName; }

	private:
		TerminalSession(Scope<PseudoConsole> console, std::string profileName,
						std::uint16_t columns, std::uint16_t rows);

		void ReadLoop();

		// Declared before the thread so that it outlives it: the members are destroyed in
		// reverse order, and the pseudo-console's own destructor reads from the pipe, which
		// must not happen while a reader is still inside it. The destructor's join is what
		// guarantees that, and nothing here substitutes for it — ~std::thread on a joinable
		// thread calls std::terminate, so removing the join ends the process before member
		// destruction order ever comes into play. This order is a second line of defence for
		// the ordering alone: it keeps the console alive across the join it follows.
		Scope<PseudoConsole> m_Console;
		std::string m_ProfileName;
		TerminalScreen m_Screen;
		VtParser m_Parser;

		std::thread m_Reader;
		std::mutex m_Mutex;
		std::condition_variable m_Drained;
		std::string m_Incoming;
		std::atomic<bool> m_Alive{true};
		std::atomic<bool> m_Stopping{false};
	};
}
