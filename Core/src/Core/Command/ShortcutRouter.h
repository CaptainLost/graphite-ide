#pragma once

#include "Core/Command/Chord.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

namespace Core {

	class CommandRegistry;
	class Keymap;

	// Turns keystrokes into commands. Each handler returns whether a command ran, which the
	// caller reports as the event being handled.
	class ShortcutRouter final
	{
	public:
		// Fires the commands bound to the pressed chord whose KeyScope is `scope`. The layer
		// routes every key event as Global; the focused editor routes the strokes it reads as
		// FocusedEditor, which is the only moment anything knows an editor holds the keyboard.
		bool OnKeyPressed(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope,
						  const KeyPressedEvent& event);

		// A tap fires on the release, because every ordinary shortcut begins with the same
		// modifiers held down.
		bool OnKeyReleased(CommandRegistry& commands, const Keymap& keys, const KeyReleasedEvent& event);

		// A modifier held for a Ctrl+click is not a tap, however it is released.
		void CancelTap() { m_TapMods = 0; }

		// What the wheel means depends on what it is pointing at, which is known only while
		// drawing, so the caller passes the scope it determined for the frame.
		bool OnMouseScrolled(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope,
							 const MouseScrolledEvent& event);

	private:
		// The modifiers held since the first of them went down, cleared the moment anything
		// else is pressed. Non-zero means a tap is still possible.
		ImGuiKeyChord m_TapMods = 0;
	};
}
