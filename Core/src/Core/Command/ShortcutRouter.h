#pragma once

#include "Core/Command/Chord.h"
#include "Core/Command/ChordFlags.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

namespace Core {

	class CommandRegistry;
	class Keymap;

	// Each handler returns true when a command is bound to the gesture, even if its Enabled gate
	// refuses to run it, so a disabled command still consumes its chord.
	class ShortcutRouter final
	{
	public:
		// Only the focused editor knows that it holds the keyboard, so the caller names the scope
		// it routes the stroke as: the layer routes every key event as Global, and the focused
		// editor routes the strokes it reads as FocusedEditor. The typing target is what held the
		// keyboard when the stroke arrived; a Global command fires only where its chord says it may.
		bool OnKeyPressed(CommandRegistry& commands, const Keymap& keymap, ShortcutScope routedScope,
						  TypingTarget typingTarget, const KeyPressedEvent& event);

		// A tap fires on the release, because every ordinary shortcut begins with the same
		// modifiers held down.
		bool OnKeyReleased(CommandRegistry& commands, const Keymap& keymap, TypingTarget typingTarget,
						   const KeyReleasedEvent& event);

		// A mouse click never reaches the key handlers, so without this a modifier held for a
		// Ctrl+click would fire as a tap when it is released.
		void CancelTap() { m_PendingTapModifiers = ImGuiMod_None; }

		// What the wheel is pointing at is known only while drawing, so the caller passes the
		// scope it found there for the frame.
		bool OnMouseScrolled(CommandRegistry& commands, const Keymap& keymap, ShortcutScope scopeUnderPointer,
							 const MouseScrolledEvent& event);

	private:
		ImGuiKeyChord m_PendingTapModifiers = ImGuiMod_None;
	};
}
