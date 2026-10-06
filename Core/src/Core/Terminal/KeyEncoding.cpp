#include "corepch.h"

#include "Core/Terminal/KeyEncoding.h"

#include "Core/Terminal/ControlCharacters.h"

namespace Core {

	namespace {
		constexpr int kUnmodifiedParameter = 1;
		constexpr int kPlaceholderFirstParameter = 1;
		constexpr int kShiftParameterBit = 1;
		constexpr int kAltParameterBit = 2;
		constexpr int kControlParameterBit = 4;

		constexpr std::string_view kBackTab = "\x1B[Z";

		constexpr int kControlCharacterOfA = 0x01;

		constexpr int kWheelUpButton = 64;
		constexpr int kWheelDownButton = 65;
		constexpr int kShiftButtonBit = 4;
		constexpr int kAltButtonBit = 8;
		constexpr int kControlButtonBit = 16;

		constexpr int kLegacyMouseOffset = 32;
		constexpr int kLargestByteValue = 255;
		constexpr int kLargestLegacyMouseValue = kLargestByteValue - kLegacyMouseOffset;

		int ModifierParameter(KeyModifier modifiers)
		{
			int parameter = kUnmodifiedParameter;

			if (HasModifier(modifiers, KeyModifier::Shift))
			{
				parameter += kShiftParameterBit;
			}

			if (HasModifier(modifiers, KeyModifier::Alt))
			{
				parameter += kAltParameterBit;
			}

			if (HasModifier(modifiers, KeyModifier::Control))
			{
				parameter += kControlParameterBit;
			}

			return parameter;
		}

		std::string ModifiedControlSequence(char finalByte, int modifierParameter)
		{
			return std::string{kControlSequenceIntroducer} + std::to_string(kPlaceholderFirstParameter) + ';' + std::to_string(modifierParameter) + finalByte;
		}

		std::string CursorSequence(char finalByte, KeyModifier modifiers)
		{
			const int modifierParameter = ModifierParameter(modifiers);

			if (modifierParameter == kUnmodifiedParameter)
			{
				return std::string{kControlSequenceIntroducer} + finalByte;
			}

			return ModifiedControlSequence(finalByte, modifierParameter);
		}

		std::string TildeSequence(int keyNumber, KeyModifier modifiers)
		{
			const int modifierParameter = ModifierParameter(modifiers);

			if (modifierParameter == kUnmodifiedParameter)
			{
				return std::string{kControlSequenceIntroducer} + std::to_string(keyNumber) + '~';
			}

			return std::string{kControlSequenceIntroducer} + std::to_string(keyNumber) + ';' + std::to_string(modifierParameter) + '~';
		}

		// SS3 (ESC O) carries no parameter, so a modified key falls back to the CSI form that does,
		// as xterm does and as a TUI reading its terminfo expects.
		std::string SingleShiftThreeSequence(char finalByte, KeyModifier modifiers)
		{
			const int modifierParameter = ModifierParameter(modifiers);

			if (modifierParameter == kUnmodifiedParameter)
			{
				return std::string("\x1BO") + finalByte;
			}

			return ModifiedControlSequence(finalByte, modifierParameter);
		}

		bool IsLetterKey(KeyCode key)
		{
			return key >= KeyCode::A && key <= KeyCode::Z;
		}

		int LetterIndexOf(KeyCode letterKey)
		{
			return static_cast<int>(letterKey) - static_cast<int>(KeyCode::A);
		}

		// The legacy form packs each value into one byte, offset to keep it printable, so it cannot
		// name a cell past the 223rd; the last one it can is reported instead.
		char LegacyMouseByte(int value)
		{
			return static_cast<char>(kLegacyMouseOffset + std::min(value, kLargestLegacyMouseValue));
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
			if (HasModifier(modifiers, KeyModifier::Shift))
			{
				return std::string{kBackTab};
			}

			return "\t";
		case KeyCode::Escape:
			return std::string(1, kEscapeCharacter);
		case KeyCode::Backspace:
			// Sending backspace (0x08) instead makes Backspace move the cursor without erasing in a
			// shell's line editing.
			return std::string(1, kDeleteCharacter);
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
			return SingleShiftThreeSequence('P', modifiers);
		case KeyCode::F2:
			return SingleShiftThreeSequence('Q', modifiers);
		case KeyCode::F3:
			return SingleShiftThreeSequence('R', modifiers);
		case KeyCode::F4:
			return SingleShiftThreeSequence('S', modifiers);
		// The tilde numbers skip 16 and 22, which were never assigned, so they are written out:
		// a formula from the key would send the wrong sequence from F6 on.
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

		if (!IsLetterKey(key))
		{
			return {};
		}

		const int letterIndex = LetterIndexOf(key);

		if (HasModifier(modifiers, KeyModifier::Control))
		{
			const char controlCharacter = static_cast<char>(kControlCharacterOfA + letterIndex);
			return std::string(1, controlCharacter);
		}

		if (HasModifier(modifiers, KeyModifier::Alt))
		{
			const char lowercaseLetter = static_cast<char>('a' + letterIndex);
			return std::string{kEscapeCharacter, lowercaseLetter};
		}

		return {};
	}

	std::string EncodeMouseWheel(WheelDirection direction, std::uint16_t zeroBasedColumn, std::uint16_t zeroBasedRow,
								 KeyModifier modifiers, MouseReportForm form)
	{
		int button = direction == WheelDirection::Up ? kWheelUpButton : kWheelDownButton;

		if (HasModifier(modifiers, KeyModifier::Shift))
		{
			button += kShiftButtonBit;
		}

		if (HasModifier(modifiers, KeyModifier::Alt))
		{
			button += kAltButtonBit;
		}

		if (HasModifier(modifiers, KeyModifier::Control))
		{
			button += kControlButtonBit;
		}

		const int oneBasedColumn = zeroBasedColumn + 1;
		const int oneBasedRow = zeroBasedRow + 1;

		if (form == MouseReportForm::SgrDecimal)
		{
			return std::string{kControlSequenceIntroducer} + "<" + std::to_string(button) + ";" + std::to_string(oneBasedColumn) + ";" + std::to_string(oneBasedRow) + "M";
		}

		return std::string{kControlSequenceIntroducer} + "M" + LegacyMouseByte(button) + LegacyMouseByte(oneBasedColumn) + LegacyMouseByte(oneBasedRow);
	}
}
