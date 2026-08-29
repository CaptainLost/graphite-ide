#include "corepch.h"

#include "Core/Term/VtParser.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr char Escape = '\x1B';
		constexpr char Bell = '\x07';

		bool IsCsiFinal(char byte)
		{
			return byte >= '@' && byte <= '~';
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
		m_Intermediate.clear();
		m_OscBuffer.clear();
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
		switch (m_State)
		{
		case State::Ground:
		{
			if (byte == Escape)
			{
				Reset();
				m_State = State::Escape;
				return;
			}

			if (static_cast<unsigned char>(byte) < 0x20)
			{
				HandleControl(byte);
				return;
			}

			// Task 7 replaces this with incremental UTF-8 decoding. Until then only the ASCII
			// range prints, which is all these tests need.
			m_Screen.Print(static_cast<char32_t>(static_cast<unsigned char>(byte)), 1);
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

			HandleEscape(byte);
			Reset();
			return;
		}
		case State::CsiEntry:
		case State::CsiParam:
		{
			if (byte == '?' && m_State == State::CsiEntry)
			{
				m_Private = true;
				m_State = State::CsiParam;
				return;
			}

			if (byte >= '0' && byte <= '9')
			{
				if (m_Parameters.empty())
				{
					m_Parameters.push_back(0);
				}

				m_Parameters.back() = static_cast<std::uint16_t>(
					std::min(m_Parameters.back() * 10 + (byte - '0'), 65535));
				m_State = State::CsiParam;
				return;
			}

			if (byte == ';')
			{
				m_Parameters.push_back(0);
				m_State = State::CsiParam;
				return;
			}

			if (byte >= ' ' && byte <= '/')
			{
				m_Intermediate.push_back(byte);
				m_State = State::CsiIntermediate;
				return;
			}

			if (IsCsiFinal(byte))
			{
				HandleCsi(byte, replies);
				Reset();
				return;
			}

			Reset();
			return;
		}
		case State::CsiIntermediate:
		{
			if (byte >= ' ' && byte <= '/')
			{
				m_Intermediate.push_back(byte);
				return;
			}

			if (IsCsiFinal(byte))
			{
				HandleCsi(byte, replies);
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

			// The other terminator is ESC \, whose ESC arrives here first.
			if (byte == Escape)
			{
				HandleOsc();
				Reset();
				m_State = State::Escape;
				return;
			}

			m_OscBuffer.push_back(byte);
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
			m_Screen.EraseInDisplay(EraseExtent::All);
			m_Screen.SetCursor(0, 0);
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
				replies += "\x1B[" + std::to_string(cursor.Row + 1) + ';'
						   + std::to_string(cursor.Column + 1) + 'R';
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
		// Task 6.
	}

	void VtParser::HandleMode(bool set)
	{
		// Task 6.
		(void)set;
	}
}
