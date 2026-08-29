#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	// A shell running behind a pseudo-console: the operating system tells it that it is
	// attached to a window of the given size, so it keeps its colours, its prompt and its
	// interactivity instead of the reduced behaviour every program falls back to when it
	// detects a pipe. That is the whole reason this exists next to ChildProcess rather than
	// being a flag on it — ChildProcess deliberately carries a framed protocol on a pipe,
	// which is the opposite requirement.
	//
	// Nothing here interprets a byte. What arrives is a stream of text and escape sequences
	// for a terminal emulator to make sense of.
	class PseudoConsole : NonCopyable
	{
	public:
		static Result<Scope<PseudoConsole>> Start(const std::filesystem::path& program,
												  const std::vector<std::string>& arguments,
												  const std::filesystem::path& workingDirectory,
												  std::uint16_t columns, std::uint16_t rows);

		~PseudoConsole();

		// Blocks until at least one byte arrives, and returns how many were written into the
		// buffer.
		//
		// Zero means "no bytes, and this call is not going to produce any" — it does NOT on its
		// own mean the shell has exited. A live shell never reports end-of-output the way a pipe
		// does, so the ordinary reason for zero is indeed that the shell is gone; but zero is
		// also what an empty buffer, a failure to set up the wait, or a failed read returns,
		// because this call has nowhere to put an error. A caller that reacts to the shell
		// exiting — closing a terminal tab, say — must confirm with IsRunning() before treating
		// zero as the end, and may otherwise read again.
		std::size_t Read(std::span<char> buffer);

		bool Write(std::string_view bytes);

		// Tells the shell its window changed, which is what makes it redraw its prompt at the
		// new width instead of leaving the old one wrapped.
		Result<void> Resize(std::uint16_t columns, std::uint16_t rows);

		// Kills the shell without closing this side's handles, releasing a thread blocked in
		// Read — the same arrangement ChildProcess::Terminate documents, and for the same
		// reason: destroying the object under that thread would pull the handle from beneath
		// it.
		void Terminate();

		bool IsRunning() const;

	private:
		PseudoConsole() = default;

		void* m_Console = nullptr;
		void* m_Process = nullptr;
		void* m_ToChild = nullptr;
		void* m_FromChild = nullptr;
		void* m_Attributes = nullptr;
	};
}
