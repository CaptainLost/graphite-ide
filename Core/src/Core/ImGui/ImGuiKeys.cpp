#include "corepch.h"

#include "Core/ImGui/ImGuiKeys.h"

namespace Core {

	namespace {
		constexpr ImGuiKeyChord kNoModifier = 0;
	}

	ImGuiKey ToImGuiKey(KeyCode key)
	{
		switch (key)
		{
		case KeyCode::Space:
			return ImGuiKey_Space;
		case KeyCode::Apostrophe:
			return ImGuiKey_Apostrophe;
		case KeyCode::Comma:
			return ImGuiKey_Comma;
		case KeyCode::Minus:
			return ImGuiKey_Minus;
		case KeyCode::Period:
			return ImGuiKey_Period;
		case KeyCode::Slash:
			return ImGuiKey_Slash;

		case KeyCode::Digit0:
			return ImGuiKey_0;
		case KeyCode::Digit1:
			return ImGuiKey_1;
		case KeyCode::Digit2:
			return ImGuiKey_2;
		case KeyCode::Digit3:
			return ImGuiKey_3;
		case KeyCode::Digit4:
			return ImGuiKey_4;
		case KeyCode::Digit5:
			return ImGuiKey_5;
		case KeyCode::Digit6:
			return ImGuiKey_6;
		case KeyCode::Digit7:
			return ImGuiKey_7;
		case KeyCode::Digit8:
			return ImGuiKey_8;
		case KeyCode::Digit9:
			return ImGuiKey_9;

		case KeyCode::Semicolon:
			return ImGuiKey_Semicolon;
		case KeyCode::Equal:
			return ImGuiKey_Equal;

		case KeyCode::A:
			return ImGuiKey_A;
		case KeyCode::B:
			return ImGuiKey_B;
		case KeyCode::C:
			return ImGuiKey_C;
		case KeyCode::D:
			return ImGuiKey_D;
		case KeyCode::E:
			return ImGuiKey_E;
		case KeyCode::F:
			return ImGuiKey_F;
		case KeyCode::G:
			return ImGuiKey_G;
		case KeyCode::H:
			return ImGuiKey_H;
		case KeyCode::I:
			return ImGuiKey_I;
		case KeyCode::J:
			return ImGuiKey_J;
		case KeyCode::K:
			return ImGuiKey_K;
		case KeyCode::L:
			return ImGuiKey_L;
		case KeyCode::M:
			return ImGuiKey_M;
		case KeyCode::N:
			return ImGuiKey_N;
		case KeyCode::O:
			return ImGuiKey_O;
		case KeyCode::P:
			return ImGuiKey_P;
		case KeyCode::Q:
			return ImGuiKey_Q;
		case KeyCode::R:
			return ImGuiKey_R;
		case KeyCode::S:
			return ImGuiKey_S;
		case KeyCode::T:
			return ImGuiKey_T;
		case KeyCode::U:
			return ImGuiKey_U;
		case KeyCode::V:
			return ImGuiKey_V;
		case KeyCode::W:
			return ImGuiKey_W;
		case KeyCode::X:
			return ImGuiKey_X;
		case KeyCode::Y:
			return ImGuiKey_Y;
		case KeyCode::Z:
			return ImGuiKey_Z;

		case KeyCode::LeftBracket:
			return ImGuiKey_LeftBracket;
		case KeyCode::Backslash:
			return ImGuiKey_Backslash;
		case KeyCode::RightBracket:
			return ImGuiKey_RightBracket;
		case KeyCode::GraveAccent:
			return ImGuiKey_GraveAccent;

		case KeyCode::Escape:
			return ImGuiKey_Escape;
		case KeyCode::Enter:
			return ImGuiKey_Enter;
		case KeyCode::Tab:
			return ImGuiKey_Tab;
		case KeyCode::Backspace:
			return ImGuiKey_Backspace;
		case KeyCode::Insert:
			return ImGuiKey_Insert;
		case KeyCode::Delete:
			return ImGuiKey_Delete;
		case KeyCode::Right:
			return ImGuiKey_RightArrow;
		case KeyCode::Left:
			return ImGuiKey_LeftArrow;
		case KeyCode::Down:
			return ImGuiKey_DownArrow;
		case KeyCode::Up:
			return ImGuiKey_UpArrow;
		case KeyCode::PageUp:
			return ImGuiKey_PageUp;
		case KeyCode::PageDown:
			return ImGuiKey_PageDown;
		case KeyCode::Home:
			return ImGuiKey_Home;
		case KeyCode::End:
			return ImGuiKey_End;
		case KeyCode::CapsLock:
			return ImGuiKey_CapsLock;
		case KeyCode::ScrollLock:
			return ImGuiKey_ScrollLock;
		case KeyCode::NumLock:
			return ImGuiKey_NumLock;
		case KeyCode::PrintScreen:
			return ImGuiKey_PrintScreen;
		case KeyCode::Pause:
			return ImGuiKey_Pause;

		case KeyCode::F1:
			return ImGuiKey_F1;
		case KeyCode::F2:
			return ImGuiKey_F2;
		case KeyCode::F3:
			return ImGuiKey_F3;
		case KeyCode::F4:
			return ImGuiKey_F4;
		case KeyCode::F5:
			return ImGuiKey_F5;
		case KeyCode::F6:
			return ImGuiKey_F6;
		case KeyCode::F7:
			return ImGuiKey_F7;
		case KeyCode::F8:
			return ImGuiKey_F8;
		case KeyCode::F9:
			return ImGuiKey_F9;
		case KeyCode::F10:
			return ImGuiKey_F10;
		case KeyCode::F11:
			return ImGuiKey_F11;
		case KeyCode::F12:
			return ImGuiKey_F12;

		case KeyCode::Keypad0:
			return ImGuiKey_Keypad0;
		case KeyCode::Keypad1:
			return ImGuiKey_Keypad1;
		case KeyCode::Keypad2:
			return ImGuiKey_Keypad2;
		case KeyCode::Keypad3:
			return ImGuiKey_Keypad3;
		case KeyCode::Keypad4:
			return ImGuiKey_Keypad4;
		case KeyCode::Keypad5:
			return ImGuiKey_Keypad5;
		case KeyCode::Keypad6:
			return ImGuiKey_Keypad6;
		case KeyCode::Keypad7:
			return ImGuiKey_Keypad7;
		case KeyCode::Keypad8:
			return ImGuiKey_Keypad8;
		case KeyCode::Keypad9:
			return ImGuiKey_Keypad9;
		case KeyCode::KeypadDecimal:
			return ImGuiKey_KeypadDecimal;
		case KeyCode::KeypadDivide:
			return ImGuiKey_KeypadDivide;
		case KeyCode::KeypadMultiply:
			return ImGuiKey_KeypadMultiply;
		case KeyCode::KeypadSubtract:
			return ImGuiKey_KeypadSubtract;
		case KeyCode::KeypadAdd:
			return ImGuiKey_KeypadAdd;
		case KeyCode::KeypadEnter:
			return ImGuiKey_KeypadEnter;
		case KeyCode::KeypadEqual:
			return ImGuiKey_KeypadEqual;

		case KeyCode::LeftShift:
			return ImGuiKey_LeftShift;
		case KeyCode::LeftControl:
			return ImGuiKey_LeftCtrl;
		case KeyCode::LeftAlt:
			return ImGuiKey_LeftAlt;
		case KeyCode::LeftSuper:
			return ImGuiKey_LeftSuper;
		case KeyCode::RightShift:
			return ImGuiKey_RightShift;
		case KeyCode::RightControl:
			return ImGuiKey_RightCtrl;
		case KeyCode::RightAlt:
			return ImGuiKey_RightAlt;
		case KeyCode::RightSuper:
			return ImGuiKey_RightSuper;
		case KeyCode::Menu:
			return ImGuiKey_Menu;
		}

		return ImGuiKey_None;
	}

	ImGuiKeyChord ModifierBitOf(KeyCode key)
	{
		switch (key)
		{
		case KeyCode::LeftControl:
		case KeyCode::RightControl:
			return ImGuiMod_Ctrl;
		case KeyCode::LeftShift:
		case KeyCode::RightShift:
			return ImGuiMod_Shift;
		case KeyCode::LeftAlt:
		case KeyCode::RightAlt:
			return ImGuiMod_Alt;
		case KeyCode::LeftSuper:
		case KeyCode::RightSuper:
			return ImGuiMod_Super;
		default:
			return kNoModifier;
		}
	}

	ImGuiKeyChord ToImGuiModifiers(KeyModifier modifiers)
	{
		ImGuiKeyChord chord = kNoModifier;

		if (HasModifier(modifiers, KeyModifier::Control))
		{
			chord |= ImGuiMod_Ctrl;
		}
		if (HasModifier(modifiers, KeyModifier::Shift))
		{
			chord |= ImGuiMod_Shift;
		}
		if (HasModifier(modifiers, KeyModifier::Alt))
		{
			chord |= ImGuiMod_Alt;
		}
		if (HasModifier(modifiers, KeyModifier::Super))
		{
			chord |= ImGuiMod_Super;
		}

		return chord;
	}
}
