#include "corepch.h"

#include "Core/Term/KeyEncoding.h"

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
	}

	std::string EncodeKey(KeyCode key, KeyModifier modifiers)
	{
		switch (key)
		{
		case KeyCode::Enter:
		case KeyCode::KPEnter:
			return "\r";
		case KeyCode::Tab:
			return "\t";
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
		case KeyCode::Delete:
			return TildeSequence(3, modifiers);
		case KeyCode::PageUp:
			return TildeSequence(5, modifiers);
		case KeyCode::PageDown:
			return TildeSequence(6, modifiers);
		default:
			break;
		}

		const auto code = static_cast<std::uint16_t>(key);

		if (HasModifier(modifiers, KeyModifier::Control) && code >= static_cast<std::uint16_t>(KeyCode::A)
			&& code <= static_cast<std::uint16_t>(KeyCode::Z))
		{
			return std::string(1, static_cast<char>(code - static_cast<std::uint16_t>(KeyCode::A) + 1));
		}

		if (HasModifier(modifiers, KeyModifier::Alt) && code >= static_cast<std::uint16_t>(KeyCode::A)
			&& code <= static_cast<std::uint16_t>(KeyCode::Z))
		{
			const char letter =
				static_cast<char>(code - static_cast<std::uint16_t>(KeyCode::A) + 'a');
			return std::string("\x1B") + letter;
		}

		return {};
	}
}
