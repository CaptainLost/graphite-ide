#include "corepch.h"

#include "Core/Terminal/KeyEncoding.h"

namespace Core {

	namespace {
		// The modifier encoding shared by every parameterised sequence: 1 plus a bit each for
		// shift, alt and control.
		int ModifierParameter(KeyModifier modifiers)
		{
			int value = 1;

			if (HasModifier(modifiers, KeyModifier::Shift))
			{
				value += 1;
			}

			if (HasModifier(modifiers, KeyModifier::Alt))
			{
				value += 2;
			}

			if (HasModifier(modifiers, KeyModifier::Control))
			{
				value += 4;
			}

			return value;
		}

		std::string CursorSequence(char final, KeyModifier modifiers)
		{
			const int parameter = ModifierParameter(modifiers);

			if (parameter == 1)
			{
				return std::string("\x1B[") + final;
			}

			return "\x1B[1;" + std::to_string(parameter) + final;
		}

		std::string TildeSequence(int number, KeyModifier modifiers)
		{
			const int parameter = ModifierParameter(modifiers);

			if (parameter == 1)
			{
				return "\x1B[" + std::to_string(number) + '~';
			}

			return "\x1B[" + std::to_string(number) + ';' + std::to_string(parameter) + '~';
		}

		// F1 to F4 are SS3 sequences — ESC O P through ESC O S — while every other function key
		// is a tilde sequence. SS3 carries no parameter, so a modified F1 has to fall back to
		// the CSI form that does; xterm does the same, and a TUI reading its terminfo expects it.
		std::string FunctionSequence(char final, KeyModifier modifiers)
		{
			const int parameter = ModifierParameter(modifiers);

			if (parameter == 1)
			{
				return std::string("\x1BO") + final;
			}

			return "\x1B[1;" + std::to_string(parameter) + final;
		}
	}

	std::string EncodeKey(KeyCode key, KeyModifier modifiers)
	{
		switch (key)
		{
		case KeyCode::Enter:
		case KeyCode::KeypadEnter:
			return "\r";
		case KeyCode::Tab:
			// Shift+Tab is CBT, the back-tab: it is how a TUI walks its focus backwards, and a
			// plain tab in its place walks forwards instead — the opposite of what was asked.
			return HasModifier(modifiers, KeyModifier::Shift) ? "\x1B[Z" : "\t";
		case KeyCode::Escape:
			return "\x1B";
		case KeyCode::Backspace:
			// Delete rather than backspace: it is what a shell's line editing expects, and
			// sending 0x08 instead makes Backspace move the cursor without erasing.
			return "\x7F";
		case KeyCode::Up:
			return CursorSequence('A', modifiers);
		case KeyCode::Down:
			return CursorSequence('B', modifiers);
		case KeyCode::Right:
			return CursorSequence('C', modifiers);
		case KeyCode::Left:
			return CursorSequence('D', modifiers);
		case KeyCode::Home:
			return CursorSequence('H', modifiers);
		case KeyCode::End:
			return CursorSequence('F', modifiers);
		case KeyCode::Insert:
			return TildeSequence(2, modifiers);
		case KeyCode::Delete:
			return TildeSequence(3, modifiers);
		case KeyCode::PageUp:
			return TildeSequence(5, modifiers);
		case KeyCode::PageDown:
			return TildeSequence(6, modifiers);
		case KeyCode::F1:
			return FunctionSequence('P', modifiers);
		case KeyCode::F2:
			return FunctionSequence('Q', modifiers);
		case KeyCode::F3:
			return FunctionSequence('R', modifiers);
		case KeyCode::F4:
			return FunctionSequence('S', modifiers);
		// The tilde numbers are not consecutive — 16 and 22 were never assigned — so they are
		// written out rather than computed from the key. A formula here would be shorter and
		// would send F6 as F5's sequence.
		case KeyCode::F5:
			return TildeSequence(15, modifiers);
		case KeyCode::F6:
			return TildeSequence(17, modifiers);
		case KeyCode::F7:
			return TildeSequence(18, modifiers);
		case KeyCode::F8:
			return TildeSequence(19, modifiers);
		case KeyCode::F9:
			return TildeSequence(20, modifiers);
		case KeyCode::F10:
			return TildeSequence(21, modifiers);
		case KeyCode::F11:
			return TildeSequence(23, modifiers);
		case KeyCode::F12:
			return TildeSequence(24, modifiers);
		default:
			break;
		}

		const auto code = static_cast<std::uint16_t>(key);

		if (HasModifier(modifiers, KeyModifier::Control) && code >= static_cast<std::uint16_t>(KeyCode::A) && code <= static_cast<std::uint16_t>(KeyCode::Z))
		{
			return std::string(1, static_cast<char>(code - static_cast<std::uint16_t>(KeyCode::A) + 1));
		}

		if (HasModifier(modifiers, KeyModifier::Alt) && code >= static_cast<std::uint16_t>(KeyCode::A) && code <= static_cast<std::uint16_t>(KeyCode::Z))
		{
			const char letter =
				static_cast<char>(code - static_cast<std::uint16_t>(KeyCode::A) + 'a');
			return std::string("\x1B") + letter;
		}

		return {};
	}

	std::string EncodeMouseWheel(bool up, std::uint16_t column, std::uint16_t row,
								 KeyModifier modifiers, bool sgr)
	{
		// Wheel buttons are 4 and 5, which xterm numbers from 64; the modifiers ride on top.
		int button = up ? 64 : 65;

		if (HasModifier(modifiers, KeyModifier::Shift))
		{
			button += 4;
		}

		if (HasModifier(modifiers, KeyModifier::Alt))
		{
			button += 8;
		}

		if (HasModifier(modifiers, KeyModifier::Control))
		{
			button += 16;
		}

		const int x = column + 1;
		const int y = row + 1;

		if (sgr)
		{
			return "[<" + std::to_string(button) + ";" + std::to_string(x) + ";" +
				   std::to_string(y) + "M";
		}

		// Each value is offset by 32 to keep it printable, so 223 is the last one a byte holds.
		const auto packed = [](int value) { return static_cast<char>(32 + std::min(value, 223)); };

		return std::string("[M") + packed(button) + packed(x) + packed(y);
	}
}
