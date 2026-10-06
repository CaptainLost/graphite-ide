#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Platform/PseudoConsole.h"
#include "Core/Platform/ShellProfiles.h"
#include "Core/Terminal/TerminalScreen.h"
#include "Core/Terminal/VtParser.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Core {

	// Free rather than a private step of Paste so that the choice can be tested against a screen
	// alone, with no shell to spawn and no wire to watch.
	std::string BracketedPasteBytes(std::string_view text, bool shellTurnedBracketedPasteOn);

	class TerminalSession : NonCopyable
	{
	public:
		static Result<Scope<TerminalSession>> Start(const ShellProfile& profile,
													const std::filesystem::path& workingDirectory,
													std::uint16_t columns, std::uint16_t rows);

		~TerminalSession();

		// UI thread only. The reader thread only queues raw bytes and this parses them, so the
		// screen is touched by one thread and needs no lock; a lock around it would be held for the
		// whole of drawing.
		void ApplyQueuedOutput();

		void Send(std::string_view bytes);

		// The paste markers stop the shell from running a multi-line paste line by line as it
		// arrives.
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

		// Declared before the reader thread so that it is destroyed after it, because
		// ~PseudoConsole reads from the pipe the reader may still be inside. Only a second line of
		// defence: destroying a joinable thread calls std::terminate, so the destructor's join is
		// what keeps the two apart.
		Scope<PseudoConsole> m_Console;
		std::string m_ProfileName;
		TerminalScreen m_Screen;
		VtParser m_Parser;

		std::mutex m_QueueMutex;
		std::condition_variable m_ReaderWake;
		std::string m_QueuedOutput;
		std::atomic<bool> m_Alive{true};
		std::atomic<bool> m_Stopping{false};

		// Last, so that everything ReadLoop touches outlives the reader should the destructor's
		// join ever be bypassed.
		std::thread m_ReaderThread;
	};
}
