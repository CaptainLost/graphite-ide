#pragma once

#include "Core/Term/TerminalScreen.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

	// Turns a byte stream from a pseudo-console into operations on a TerminalScreen.
	//
	// It is a state machine rather than a ladder of conditionals for one property: an
	// unrecognised sequence is consumed in full, so its tail never lands on the screen as
	// text. With console parity as the bar, what is not handled matters as much as what is.
	//
	// State is kept across calls because a read from a pipe can end anywhere, including in
	// the middle of an escape sequence or of a UTF-8 character.
	class VtParser
	{
	public:
		explicit VtParser(TerminalScreen& screen) : m_Screen(screen) {}

		// Returns the bytes the terminal owes the program in reply. Some sequences are
		// questions — where is the cursor, what kind of terminal are you — and a program that
		// asks one waits for the answer, so a caller must write this back.
		std::string Consume(std::string_view bytes);

	private:
		enum class State : std::uint8_t
		{
			Ground,
			Escape,
			// An intermediate byte followed the ESC: "ESC ( B" selects a character set,
			// "ESC # 8" is the DEC alignment test. The final byte after it is dispatched
			// nowhere, but it must be consumed here rather than printed.
			EscapeIntermediate,
			CsiEntry,
			CsiParam,
			CsiIntermediate,
			// A CSI this parser does not interpret (colon sub-parameters, a misplaced
			// prefix byte): consumed up to its final byte, which dispatches nothing.
			CsiIgnore,
			OscString,
			// An ESC arrived inside an OSC: the next byte decides whether it was the string
			// terminator ESC \ or an abort.
			OscEscape,
			StringSink
		};

		void ConsumeByte(char byte, std::string& replies);
		void PrintByte(char byte);
		void HandleControl(char byte);
		void HandleEscape(char byte);
		void HandleCsi(char final, std::string& replies);
		void HandleOsc();
		void HandleSgr();
		void HandleMode(bool set);
		void Reset();

		std::uint16_t Parameter(std::size_t index, std::uint16_t fallback) const;

		TerminalScreen& m_Screen;
		State m_State = State::Ground;
		std::vector<std::uint16_t> m_Parameters;
		// Set once the parameter list is full: further digits and separators are dropped
		// rather than corrupting the last parameter.
		bool m_ParametersFull = false;
		std::string m_Intermediate;
		std::string m_OscBuffer;
		// The byte a CSI was opened with, one of 0x3C-0x3F ('<', '=', '>', '?'), or '\0' when
		// there was none. Only '?' shares dispatch with an unprefixed sequence; the rest
		// introduce sequences this terminal does not implement and must be swallowed whole.
		char m_Prefix = '\0';
		// Set for a CSI opened with '?', which marks the DEC private modes — the alternate
		// buffer, cursor visibility, bracketed paste. They share final bytes with the public
		// modes and mean entirely different things.
		bool m_Private = false;
		// A read from a pipe can end in the middle of a multi-byte character, so the partial
		// bytes are held here until the character completes.
		char32_t m_Pending = 0;
		std::uint8_t m_PendingBytes = 0;
	};
}
