#include "corepch.h"

#include "Core/Terminal/VtParser.h"

#include "Core/Terminal/CharacterWidth.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr char Escape = '\x1B';
		constexpr char Bell = '\x07';
		constexpr char Delete = '\x7F';

		// This parser is where bytes from an arbitrary program enter the process, so neither
		// buffer may grow with the stream: a run of ';' or an OSC that never terminates would
		// otherwise reallocate until the process runs out of memory. Real terminals cap both
		// at about these sizes, and no sequence anyone emits comes near them. Past the cap the
		// excess is dropped, never buffered.
		constexpr std::size_t MaxParameters = 32;
		constexpr std::size_t MaxOscLength = 4096;
		// No real sequence uses more than two intermediates, so a handful of bytes is plenty;
		// without a cap "ESC[" followed by an endless run of 0x20-0x2F bytes would grow this
		// buffer forever.
		constexpr std::size_t MaxIntermediateLength = 8;

		bool IsCsiFinal(char byte)
		{
			return byte >= '@' && byte <= '~';
		}

		// 0x3C-0x3F: '<', '=', '>', '?'. All four are legal parameter-prefix bytes and real
		// programs emit them — ESC[>0c for secondary device attributes, ESC[>4;2m for
		// modifyOtherKeys — so they must be parsed, not aborted on.
		bool IsCsiPrefix(char byte)
		{
			return byte >= '<' && byte <= '?';
		}
	}

	std::string VtParser::Consume(std::string_view bytes)
	{
		std::string replies;

		for (const char byte : bytes)
		{
			ConsumeByte(byte, replies);
		}

		return replies;
	}

	void VtParser::Reset()
	{
		m_State = State::Ground;
		m_Parameters.clear();
		m_ParametersFull = false;
		m_Intermediate.clear();
		m_OscBuffer.clear();
		m_Prefix = '\0';
		m_Private = false;
	}

	std::uint16_t VtParser::Parameter(std::size_t index, std::uint16_t fallback) const
	{
		if (index >= m_Parameters.size() || m_Parameters[index] == 0)
		{
			return fallback;
		}

		return m_Parameters[index];
	}

	void VtParser::ConsumeByte(char byte, std::string& replies)
	{
		// An ESC aborts whatever is in progress and begins a new sequence, in every state
		// where it cannot be part of a terminator. Letting it fall through to the reset below
		// instead would consume the ESC of the *next* sequence, and that sequence's body would
		// then land on the screen as text — the one thing this parser exists to prevent.
		// The string states own their ESC: there it may be the first half of ESC \.
		if (byte == Escape && m_State != State::OscString && m_State != State::OscEscape && m_State != State::StringSink)
		{
			Reset();
			m_State = State::Escape;
			return;
		}

		switch (m_State)
		{
		case State::Ground:
		{
			if (static_cast<unsigned char>(byte) < 0x20)
			{
				HandleControl(byte);
				return;
			}

			// DEL has no glyph and no effect; a terminal discards it.
			if (byte == Delete)
			{
				return;
			}

			PrintByte(byte);
			return;
		}
		case State::Escape:
		{
			if (byte == '[')
			{
				m_State = State::CsiEntry;
				return;
			}

			if (byte == ']')
			{
				m_State = State::OscString;
				return;
			}

			// A device control or privacy message: consumed to its terminator and dropped.
			if (byte == 'P' || byte == '^' || byte == '_')
			{
				m_State = State::StringSink;
				return;
			}

			// An intermediate byte: the sequence's meaning is decided by the final byte still
			// to come, so nothing can be dispatched yet and the ESC path needs the same
			// intermediate state the CSI path already has. Without it "ESC ( B" — ncurses'
			// sgr0 for the xterm terminfo, emitted on every attribute reset by vim, less,
			// htop and git's pager — swallows the '(' and prints the 'B'.
			if (byte >= ' ' && byte <= '/')
			{
				if (m_Intermediate.size() < MaxIntermediateLength)
				{
					m_Intermediate.push_back(byte);
				}

				m_State = State::EscapeIntermediate;
				return;
			}

			HandleEscape(byte);
			Reset();
			return;
		}
		case State::EscapeIntermediate:
		{
			if (byte >= ' ' && byte <= '/')
			{
				if (m_Intermediate.size() < MaxIntermediateLength)
				{
					m_Intermediate.push_back(byte);
				}

				return;
			}

			// Nothing with an intermediate is implemented — character set selection, the
			// alignment test — so the final byte dispatches nothing and is consumed whole,
			// which is the invariant this parser exists for.
			Reset();
			return;
		}
		case State::CsiEntry:
		case State::CsiParam:
		{
			if (m_State == State::CsiEntry && IsCsiPrefix(byte))
			{
				m_Prefix = byte;
				// Only '?' means DEC private; the others are recorded as themselves so that
				// dispatch can tell them apart rather than treating them as private modes.
				m_Private = byte == '?';
				m_State = State::CsiParam;
				return;
			}

			if (byte >= '0' && byte <= '9')
			{
				m_State = State::CsiParam;

				if (m_ParametersFull)
				{
					return;
				}

				if (m_Parameters.empty())
				{
					m_Parameters.push_back(0);
				}

				m_Parameters.back() = static_cast<std::uint16_t>(
					std::min(m_Parameters.back() * 10 + (byte - '0'), 65535));
				return;
			}

			if (byte == ';')
			{
				m_State = State::CsiParam;

				if (m_ParametersFull)
				{
					return;
				}

				// The slot the separator closes has to exist before the next one opens, or a
				// leading empty parameter — "ESC[;5H" is row default, column five — is lost.
				if (m_Parameters.empty())
				{
					m_Parameters.push_back(0);
				}

				if (m_Parameters.size() >= MaxParameters)
				{
					m_ParametersFull = true;
					return;
				}

				m_Parameters.push_back(0);
				return;
			}

			if (byte >= ' ' && byte <= '/')
			{
				if (m_Intermediate.size() < MaxIntermediateLength)
				{
					m_Intermediate.push_back(byte);
				}
				m_State = State::CsiIntermediate;
				return;
			}

			if (IsCsiFinal(byte))
			{
				HandleCsi(byte, replies);
				Reset();
				return;
			}

			// A ':' sub-parameter separator ("ESC[4:3m", "ESC[38:2::r:g:bm") or a prefix byte
			// that is not first is a well-formed sequence this parser does not interpret.
			// Dropping it at the final byte keeps its tail off the screen.
			if (byte == ':' || IsCsiPrefix(byte))
			{
				m_State = State::CsiIgnore;
				return;
			}

			Reset();
			return;
		}
		case State::CsiIgnore:
		{
			// A control byte abandons the sequence, as it does in the states that interpret one.
			if (static_cast<unsigned char>(byte) < 0x20)
			{
				Reset();
				return;
			}

			if (IsCsiFinal(byte))
			{
				Reset();
			}

			return;
		}
		case State::CsiIntermediate:
		{
			if (byte >= ' ' && byte <= '/')
			{
				if (m_Intermediate.size() < MaxIntermediateLength)
				{
					m_Intermediate.push_back(byte);
				}
				return;
			}

			if (IsCsiFinal(byte))
			{
				HandleCsi(byte, replies);
			}
			else if (byte >= '0' && byte <= '?')
			{
				// A parameter byte after an intermediate is malformed; the sequence is
				// consumed to its final byte and dispatches nothing.
				m_State = State::CsiIgnore;
				return;
			}

			Reset();
			return;
		}
		case State::OscString:
		{
			if (byte == Bell)
			{
				HandleOsc();
				Reset();
				return;
			}

			// The other terminator is ESC \, whose ESC arrives here first. Whether it really
			// terminates the string is not known until the next byte.
			if (byte == Escape)
			{
				m_State = State::OscEscape;
				return;
			}

			if (m_OscBuffer.size() < MaxOscLength)
			{
				m_OscBuffer.push_back(byte);
			}

			return;
		}
		case State::OscEscape:
		{
			if (byte == '\\')
			{
				HandleOsc();
				Reset();
				return;
			}

			// A bare ESC means the program abandoned the string, so nothing of a partial
			// payload may be committed — a half-written title is worse than none. The byte
			// after it belongs to whatever sequence that ESC opened, so it is replayed there.
			Reset();
			m_State = State::Escape;
			ConsumeByte(byte, replies);
			return;
		}
		case State::StringSink:
		{
			if (byte == Bell)
			{
				Reset();
			}

			if (byte == Escape)
			{
				Reset();
				m_State = State::Escape;
			}

			return;
		}
		}
	}

	void VtParser::PrintByte(char byte)
	{
		Utf8Step step = m_Utf8.Feed(byte);

		if (step.Status == Utf8Status::Interrupted)
		{
			// The byte cannot continue the character in progress: that character is broken.
			// Show a replacement and re-read this byte as a fresh start, so one bad byte costs
			// one character rather than resynchronising the stream.
			m_Screen.Print(kReplacementCharacter, 1);
			step = m_Utf8.Feed(byte);
		}

		switch (step.Status)
		{
		case Utf8Status::Decoded:
			m_Screen.Print(step.CodePoint, CharacterWidth(step.CodePoint));
			break;
		case Utf8Status::Invalid:
			// A byte that cannot start any UTF-8 sequence — a continuation byte with none
			// expected, C0, C1 or F5-FF: one replacement, no state.
			m_Screen.Print(kReplacementCharacter, 1);
			break;
		case Utf8Status::Pending:
		case Utf8Status::Interrupted:
			// Re-read with nothing pending, a byte is never Interrupted again.
			break;
		}
	}

	void VtParser::HandleControl(char byte)
	{
		switch (byte)
		{
		case '\r':
			m_Screen.CarriageReturn();
			break;
		case '\n':
		case '\v':
		case '\f':
			m_Screen.LineFeed();
			break;
		case '\b':
			m_Screen.Backspace();
			break;
		case '\t':
			m_Screen.Tab();
			break;
		default:
			// A bell, a shift-in, a shift-out: nothing this terminal shows.
			break;
		}
	}

	void VtParser::HandleEscape(char byte)
	{
		switch (byte)
		{
		case '7':
			m_Screen.SaveCursor();
			break;
		case '8':
			m_Screen.RestoreCursor();
			break;
		case 'M':
			m_Screen.ReverseIndex();
			break;
		case 'D':
			m_Screen.LineFeed();
			break;
		case 'E':
			m_Screen.CarriageReturn();
			m_Screen.LineFeed();
			break;
		case 'c':
			// Full reset: every mode the screen tracks returns to its power-on value. The
			// alternate buffer goes first so the erase below clears the primary screen.
			m_Screen.UseAlternateBuffer(false);
			m_Screen.ResetScrollRegion();
			m_Screen.SetAutoWrap(true);
			m_Screen.SetBracketedPaste(false);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Clicks, false);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Drags, false);
			m_Screen.SetMouseMode(TerminalScreen::MouseMode::Motion, false);
			m_Screen.SetSgrMouse(false);
			m_Screen.SetCursorVisible(true);
			m_Screen.EraseInDisplay(EraseExtent::All);
			m_Screen.SetCursor(0, 0);
			// The screen has no call to clear the saved cursor, but saving home overwrites it with
			// the power-on value.
			m_Screen.SaveCursor();
			m_Screen.SetPen(Pen{});
			break;
		default:
			// Character set selection and the rest: swallowed, which is the point of routing
			// them through here rather than letting them fall to the screen.
			break;
		}
	}

	void VtParser::HandleCsi(char final, std::string& replies)
	{
		// An intermediate byte changes what a final means: "CSI Ps SP @" is scroll left, not
		// insert characters, and "CSI Ps SP A" is scroll right, not cursor up. None of those
		// are implemented, so the sequence is swallowed silently — dispatching on the final
		// alone would perform a wrong action, which is worse than performing none.
		if (!m_Intermediate.empty())
		{
			return;
		}

		// ESC[>0c asks for secondary device attributes. tmux and some ncurses probes send it
		// and wait on a timeout for an answer, so this is worth a real reply rather than the
		// silence every other '>'-prefixed sequence gets.
		if (m_Prefix == '>' && final == 'c')
		{
			replies += "\x1B[>0;10;1c";
			return;
		}

		// Likewise for the rest of a prefix other than '?': ESC[>4;2m sets modifyOtherKeys,
		// and answering it as though it were the unprefixed sequence would be a wrong answer
		// rather than silence.
		if (m_Prefix != '\0' && m_Prefix != '?')
		{
			return;
		}

		switch (final)
		{
		case 'A':
			m_Screen.MoveCursor(-static_cast<int>(Parameter(0, 1)), 0);
			break;
		case 'B':
			m_Screen.MoveCursor(static_cast<int>(Parameter(0, 1)), 0);
			break;
		case 'C':
			m_Screen.MoveCursor(0, static_cast<int>(Parameter(0, 1)));
			break;
		case 'D':
			m_Screen.MoveCursor(0, -static_cast<int>(Parameter(0, 1)));
			break;
		case 'G':
			m_Screen.SetCursor(m_Screen.GetCursor().Row,
							   static_cast<std::uint16_t>(Parameter(0, 1) - 1));
			break;
		case 'd':
			m_Screen.SetCursor(static_cast<std::uint16_t>(Parameter(0, 1) - 1),
							   m_Screen.GetCursor().Column);
			break;
		case 'H':
		case 'f':
			m_Screen.SetCursor(static_cast<std::uint16_t>(Parameter(0, 1) - 1),
							   static_cast<std::uint16_t>(Parameter(1, 1) - 1));
			break;
		case 'J':
			m_Screen.EraseInDisplay(static_cast<EraseExtent>(
				std::min<std::uint16_t>(m_Parameters.empty() ? 0 : m_Parameters[0], 2)));
			break;
		case 'K':
			m_Screen.EraseInLine(static_cast<EraseExtent>(
				std::min<std::uint16_t>(m_Parameters.empty() ? 0 : m_Parameters[0], 2)));
			break;
		case 'L':
			m_Screen.InsertLines(Parameter(0, 1));
			break;
		case 'M':
			m_Screen.DeleteLines(Parameter(0, 1));
			break;
		case '@':
			m_Screen.InsertCharacters(Parameter(0, 1));
			break;
		case 'P':
			m_Screen.DeleteCharacters(Parameter(0, 1));
			break;
		case 'X':
			m_Screen.EraseCharacters(Parameter(0, 1));
			break;
		case 'S':
			m_Screen.ScrollUp(Parameter(0, 1));
			break;
		case 'T':
			m_Screen.ScrollDown(Parameter(0, 1));
			break;
		case 'r':
			m_Screen.SetScrollRegion(
				static_cast<std::uint16_t>(Parameter(0, 1) - 1),
				static_cast<std::uint16_t>(Parameter(1, m_Screen.GetRows()) - 1));
			break;
		case 's':
			m_Screen.SaveCursor();
			break;
		case 'u':
			m_Screen.RestoreCursor();
			break;
		case 'm':
			HandleSgr();
			break;
		case 'h':
			HandleMode(true);
			break;
		case 'l':
			HandleMode(false);
			break;
		case 'c':
			// Device attributes: "I am a VT100 with an advanced video option", which is what
			// every emulator answers and what every program accepts.
			replies += "\x1B[?1;2c";
			break;
		case 'n':
			if (Parameter(0, 0) == 6)
			{
				const CursorPosition cursor = m_Screen.GetCursor();
				replies += "\x1B[" + std::to_string(cursor.Row + 1) + ';' + std::to_string(cursor.Column + 1) + 'R';
			}
			break;
		default:
			break;
		}
	}

	void VtParser::HandleOsc()
	{
		// The payload is "<number>;<text>". Only the window title is used, and it is what
		// labels a terminal tab: a shell announces what it is running through it.
		const std::size_t separator = m_OscBuffer.find(';');

		if (separator == std::string::npos)
		{
			return;
		}

		const std::string_view command{m_OscBuffer.data(), separator};

		if (command == "0" || command == "2")
		{
			m_Screen.SetTitle(m_OscBuffer.substr(separator + 1));
		}
	}

	void VtParser::HandleSgr()
	{
		if (m_Parameters.empty())
		{
			m_Parameters.push_back(0);
		}

		Pen pen = m_Screen.GetPen();

		for (std::size_t index = 0; index < m_Parameters.size(); ++index)
		{
			const std::uint16_t code = m_Parameters[index];

			switch (code)
			{
			case 0:
				pen = Pen{};
				break;
			case 1:
				pen.Flags = pen.Flags | CellFlags::Bold;
				break;
			case 2:
				pen.Flags = pen.Flags | CellFlags::Faint;
				break;
			case 3:
				pen.Flags = pen.Flags | CellFlags::Italic;
				break;
			case 4:
				pen.Flags = pen.Flags | CellFlags::Underline;
				break;
			case 7:
				pen.Flags = pen.Flags | CellFlags::Inverse;
				break;
			case 8:
				pen.Flags = pen.Flags | CellFlags::Hidden;
				break;
			case 22:
				pen.Flags = pen.Flags & ~(CellFlags::Bold | CellFlags::Faint);
				break;
			case 23:
				pen.Flags = pen.Flags & ~CellFlags::Italic;
				break;
			case 24:
				pen.Flags = pen.Flags & ~CellFlags::Underline;
				break;
			case 27:
				pen.Flags = pen.Flags & ~CellFlags::Inverse;
				break;
			case 28:
				pen.Flags = pen.Flags & ~CellFlags::Hidden;
				break;
			case 39:
				pen.Foreground = TerminalColor{};
				break;
			case 49:
				pen.Background = TerminalColor{};
				break;
			case 38:
			case 48:
			{
				// Extended colour: "38;5;<index>" or "38;2;<r>;<g>;<b>", with the parameters
				// that follow belonging to this code rather than standing on their own.
				TerminalColor chosen;
				const std::uint16_t kind = Parameter(index + 1, 0);

				if (kind == 5 && index + 2 < m_Parameters.size())
				{
					chosen = TerminalColor::FromPalette(static_cast<std::uint8_t>(m_Parameters[index + 2]));
					index += 2;
				}
				else if (kind == 2 && index + 4 < m_Parameters.size())
				{
					chosen = TerminalColor::FromRgb(static_cast<std::uint8_t>(m_Parameters[index + 2]),
													static_cast<std::uint8_t>(m_Parameters[index + 3]),
													static_cast<std::uint8_t>(m_Parameters[index + 4]));
					index += 4;
				}
				else
				{
					break;
				}

				if (code == 38)
				{
					pen.Foreground = chosen;
				}
				else
				{
					pen.Background = chosen;
				}

				break;
			}
			default:
			{
				if (code >= 30 && code <= 37)
				{
					pen.Foreground = TerminalColor::FromPalette(static_cast<std::uint8_t>(code - 30));
				}
				else if (code >= 40 && code <= 47)
				{
					pen.Background = TerminalColor::FromPalette(static_cast<std::uint8_t>(code - 40));
				}
				else if (code >= 90 && code <= 97)
				{
					pen.Foreground = TerminalColor::FromPalette(static_cast<std::uint8_t>(code - 90 + 8));
				}
				else if (code >= 100 && code <= 107)
				{
					pen.Background = TerminalColor::FromPalette(static_cast<std::uint8_t>(code - 100 + 8));
				}

				break;
			}
			}
		}

		m_Screen.SetPen(pen);
	}

	void VtParser::HandleMode(bool set)
	{
		if (!m_Private)
		{
			// The public modes (insert mode and its kind) are not used by anything this
			// terminal runs; recognising and ignoring them keeps them off the screen.
			return;
		}

		for (const std::uint16_t mode : m_Parameters)
		{
			switch (mode)
			{
			case 7:
				m_Screen.SetAutoWrap(set);
				break;
			case 25:
				m_Screen.SetCursorVisible(set);
				break;
			case 1000:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Clicks, set);
				break;
			case 1002:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Drags, set);
				break;
			case 1003:
				m_Screen.SetMouseMode(TerminalScreen::MouseMode::Motion, set);
				break;
			case 1006:
				m_Screen.SetSgrMouse(set);
				break;
			case 1049:
				m_Screen.UseAlternateBuffer(set);
				break;
			case 2004:
				m_Screen.SetBracketedPaste(set);
				break;
			default:
				// The rest are out of scope for now, and being listed nowhere is exactly what
				// keeps them harmless.
				break;
			}
		}
	}
}
