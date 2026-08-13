#pragma once

#include "Core/Command/Chord.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Events/MouseEvent.h"

namespace Core {

	class CommandRegistry;
	class Keymap;

	// Turns keystrokes into commands.
	//
	// Driven by events rather than by polling at draw time, so a keystroke is answered once,
	// by whoever is entitled to it: anything above this in the layer stack that consumed the
	// event never reaches here at all.
	class ShortcutRouter final
	{
	public:
		// Returns whether a command ran, which the caller reports as the event being handled.
		bool OnKeyPressed(CommandRegistry& commands, const Keymap& keys, const KeyPressedEvent& event);

		// The wheel is aimed: what it means depends on what it is pointing at, which is known
		// only while drawing. The caller passes the scope it determined for the frame.
		bool OnMouseScrolled(CommandRegistry& commands, const Keymap& keys, Chord::Scope scope,
							 const MouseScrolledEvent& event);
	};
}
